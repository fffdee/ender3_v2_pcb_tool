#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "motion_control.h"
#include "drv_stepper.h"
#include "banux_component.h"
#include "banux_config.h"
#include "bg_shell.h"

/* 自实现 double 平方根，避免链接 libm（Keil 默认不带数学库） */
static double dsqrt(double x)
{
    double g = 1.0;
    int i;
    if (x <= 0.0) return 0.0;
    for (i = 0; i < 12; i++) {
        double q = x / g;
        g = 0.5 * (g + q);
    }
    return g;
}

/* 单拍半周期(us)上下限，直接复用步进驱动的约束 */
#define MOTION_MIN_PULSE_US 2u
#define MOTION_MAX_PULSE_US 100000u

/* 由瞬时速度(steps/s)换算单拍半周期(us)：整周期 = 1e6 / speed */
static uint32_t pulse_us_for_speed(double speed)
{
    double half = (1000000.0 / speed) / 2.0;

    if (speed < 1.0) speed = 1.0;
    if (half < (double)MOTION_MIN_PULSE_US) half = (double)MOTION_MIN_PULSE_US;
    if (half > (double)MOTION_MAX_PULSE_US) half = (double)MOTION_MAX_PULSE_US;
    return (uint32_t)half;
}

int MotionControl_Move(DrvStepperAxis_t axis, int32_t steps,
                       double vMax, double aMax)
{
    int32_t total = steps < 0 ? -steps : steps;
    uint8_t dir = steps > 0 ? 1u : 0u;
    int32_t nAccel, nDecel, nCruise;
    double vPeak = vMax;
    double dA;
    int32_t k;

    if ((int)axis >= (int)DRV_STEPPER_COUNT || total <= 0) return -1;
    if (aMax <= 0.0) aMax = 1.0;
    if (vMax <= 0.0) vMax = 2000.0;
    if (DrvStepper_SetDirection(axis, dir) != 0) return -3;

    /* 加速段所需步数：dA = vMax^2 / (2*aMax) */
    dA = (vMax * vMax) / (2.0 * aMax);
    if (2.0 * dA >= (double)total) {
        /* 三角形：达不到最大速度，中途即开始减速 */
        nAccel = total / 2;
        nDecel = total - nAccel;
        nCruise = 0;
        vPeak = dsqrt(aMax * (double)total);
        if (vPeak > vMax) vPeak = vMax;
    } else {
        nAccel = (int32_t)dA;
        nDecel = nAccel;
        nCruise = total - 2 * nAccel;
        vPeak = vMax;
    }

    /* 加速段：速度 v(k) = dsqrt(2 * aMax * k) */
    for (k = 1; k <= nAccel; k++) {
        double v = dsqrt(2.0 * aMax * (double)k);
        if (v > vPeak) v = vPeak;
        if (DrvStepper_Step(axis, 1u, pulse_us_for_speed(v)) != 0) return -2;
    }
    /* 匀速段 */
    for (k = 0; k < nCruise; k++) {
        if (DrvStepper_Step(axis, 1u, pulse_us_for_speed(vPeak)) != 0) return -2;
    }
    /* 减速段：距终点 k 步时速度 v(k) = dsqrt(2 * aMax * k) */
    for (k = nDecel; k >= 1; k--) {
        double v = dsqrt(2.0 * aMax * (double)k);
        if (v > vPeak) v = vPeak;
        if (DrvStepper_Step(axis, 1u, pulse_us_for_speed(v)) != 0) return -2;
    }
    return 0;
}

static int shell_motion(int argc, char *argv[])
{
    DrvStepperAxis_t axis;
    int32_t steps;
    double vMax = 2000.0, aMax = 40000.0;
    const char *axisName;
    int ret;

    if (argc < 2) {
        Shell_Print("usage: motion <x|y|z|e> <steps> [vmax] [accel]\r\n");
        return -1;
    }
    axisName = argv[0];
    if (!strcmp(axisName, "x")) axis = DRV_STEPPER_X;
    else if (!strcmp(axisName, "y")) axis = DRV_STEPPER_Y;
    else if (!strcmp(axisName, "z")) axis = DRV_STEPPER_Z;
    else if (!strcmp(axisName, "e")) axis = DRV_STEPPER_E;
    else {
        Shell_Print("motion: bad axis (use x/y/z/e)\r\n");
        return -1;
    }

    steps = (int32_t)atol(argv[1]);
    if (argc > 2) vMax = strtod(argv[2], NULL);
    if (argc > 3) aMax = strtod(argv[3], NULL);

    Shell_Printf("motion: %s %ld steps vmax=%.0f accel=%.0f\r\n",
                 axisName, (long)steps, vMax, aMax);
    ret = MotionControl_Move(axis, steps, vMax, aMax);
    if (ret == 0)
        Shell_Print("motion: done\r\n");
    else if (ret == -2)
        /* -2: DrvStepper_Step 被拒（未使能或忙）。使能用 gcode M17，不是 echo enable 1 */
        Shell_Print("motion: step rejected - enable motors first (gcode M17)\r\n");
    else
        Shell_Printf("motion: failed (%d)\r\n", ret);
    return 0;
}

static const ShellOpt_t s_motionOptions[] = {
    OPT("", "", "<x|y|z|e> <steps> [vmax] [accel]",
        "Trapezoidal accel/decel move", shell_motion),
    OPT_END()
};

static const ShellModule_t s_motionModule = {
    "motion", "Trapezoidal motion control", MOD_CAT_SYSTEM,
    s_motionOptions, OPT_COUNT(s_motionOptions)
};

/* ============================================================
 * 回零 (homing)：沿配置方向移动到 min 限位触发处，位置置 0
 * ------------------------------------------------------------
 * 步骤：①起点若压住限位先慢速退出 → ②快速逼近到触发 → ③回退 backoff
 *       → ④慢速二次逼近到触发（提高重复精度）→ ⑤position=0。
 * 全程用 DrvStepper_Step 分小段走、每段查 DrvStepper_GetLimit，触发即停；
 * 走满 HOME_MAX_MM 仍未触发返回 -4（方向配反或限位故障），不会无限移动。
 * ============================================================ */
static const int32_t s_homeStepsPerMm[DRV_STEPPER_COUNT] = {
    GCODE_X_STEPS_PER_MM, GCODE_Y_STEPS_PER_MM,
    GCODE_Z_STEPS_PER_MM, GCODE_E_STEPS_PER_MM
};
static const uint8_t s_homeDir[DRV_STEPPER_COUNT] = {
    HOME_DIR_X, HOME_DIR_Y, HOME_DIR_Z, 0u
};
static const double s_homeMaxMm[DRV_STEPPER_COUNT] = {
    HOME_MAX_MM_X, HOME_MAX_MM_Y, HOME_MAX_MM_Z, 0.0
};

/* 由 mm/s 与 stepsPerMm 换算单拍半周期(us)：speed(steps/s) = mm/s * stepsPerMm */
static uint32_t home_pulse_us(double mmPerSec, int32_t stepsPerMm)
{
    double speed = mmPerSec * (double)stepsPerMm;
    double half;

    if (speed < 1.0) speed = 1.0;
    half = (1000000.0 / speed) / 2.0;
    if (half < (double)MOTION_MIN_PULSE_US) half = (double)MOTION_MIN_PULSE_US;
    if (half > (double)MOTION_MAX_PULSE_US) half = (double)MOTION_MAX_PULSE_US;
    return (uint32_t)half;
}

/* 朝 dir 方向最多走 maxSteps，每走一段查一次限位。
 * 返回 1=已触发限位, 0=走满未触发, <0=DrvStepper_Step 失败(如 -2 未使能/忙)。 */
static int home_seek(DrvStepperAxis_t axis, uint8_t dir, int32_t maxSteps,
                     int32_t chunk, uint32_t pulseUs)
{
    int32_t done = 0;

    if (maxSteps <= 0) return 0;
    DrvStepper_SetDirection(axis, dir);
    while (done < maxSteps) {
        int32_t n = maxSteps - done;
        int r;
        if (n > chunk) n = chunk;
        r = DrvStepper_Step(axis, (uint32_t)n, pulseUs);
        if (r != 0) return r;
        done += n;
        if (DrvStepper_GetLimit(axis) > 0) return 1;
    }
    return 0;
}

int MotionControl_Home(DrvStepperAxis_t axis)
{
    int32_t spm, maxSteps, backoff, fastChunk, slowChunk, guard;
    uint32_t fastUs, slowUs;
    uint8_t homeDir, awayDir;
    int r;

    if ((int)axis >= (int)DRV_STEPPER_COUNT || axis == DRV_STEPPER_E) return -1;
    if (DrvStepper_GetLimit(axis) < 0) return -1;   /* 该轴无限位 */

    spm       = s_homeStepsPerMm[axis];
    homeDir   = s_homeDir[axis];
    awayDir   = homeDir ? 0u : 1u;
    maxSteps  = (int32_t)(s_homeMaxMm[axis] * (double)spm);
    backoff   = (int32_t)(HOME_BACKOFF_MM * (double)spm);
    fastChunk = (int32_t)(HOME_FAST_CHUNK_MM * (double)spm);
    slowChunk = (int32_t)(HOME_SLOW_CHUNK_MM * (double)spm);
    if (backoff < 1) backoff = 1;
    if (fastChunk < 1) fastChunk = 1;
    if (slowChunk < 1) slowChunk = 1;
    fastUs = home_pulse_us(HOME_FAST_MM_S, spm);
    slowUs = home_pulse_us(HOME_SLOW_MM_S, spm);

    /* ① 起点已压住限位：慢速退到限位松开为止（最多 backoff*4） */
    guard = backoff * 4;
    if (DrvStepper_GetLimit(axis) > 0) {
        DrvStepper_SetDirection(axis, awayDir);
        while (guard > 0 && DrvStepper_GetLimit(axis) > 0) {
            if (DrvStepper_Step(axis, (uint32_t)slowChunk, slowUs) != 0) return -2;
            guard -= slowChunk;
        }
    }

    /* ② 快速逼近直到触发限位 */
    r = home_seek(axis, homeDir, maxSteps, fastChunk, fastUs);
    if (r < 0) return r;
    if (r == 0) return -4;

    /* ③ 回退 backoff 脱离限位 */
    DrvStepper_SetDirection(axis, awayDir);
    if (DrvStepper_Step(axis, (uint32_t)backoff, slowUs) != 0) return -2;

    /* ④ 慢速二次逼近直到再次触发 */
    r = home_seek(axis, homeDir, backoff * 2, slowChunk, slowUs);
    if (r < 0) return r;
    if (r == 0) return -4;

    /* ⑤ 触发点即机械零点 */
    return DrvStepper_SetPosition(axis, 0);
}

static int home_one(DrvStepperAxis_t axis, const char *name)
{
    int r = MotionControl_Home(axis);

    if (r == 0) {
        Shell_Printf("home: %s ok (pos=0)\r\n", name);
        return 0;
    }
    if (r == -2)
        Shell_Printf("home: %s rejected - enable motors first (echo enable 1)\r\n", name);
    else if (r == -4)
        Shell_Printf("home: %s limit not found in max travel (check HOME_DIR / wiring)\r\n", name);
    else if (r == -1)
        Shell_Printf("home: %s has no limit switch\r\n", name);
    else
        Shell_Printf("home: %s err (%d)\r\n", name, r);
    return r;
}

static int shell_home(int argc, char *argv[])
{
    const char *t = (argc > 0) ? argv[0] : "all";

    if (!strcmp(t, "all")) {
        if (home_one(DRV_STEPPER_X, "X") != 0) return 0;
        if (home_one(DRV_STEPPER_Y, "Y") != 0) return 0;
        if (home_one(DRV_STEPPER_Z, "Z") != 0) return 0;
        Shell_Print("home: done\r\n");
        return 0;
    }
    if (!strcmp(t, "x") || !strcmp(t, "X")) { home_one(DRV_STEPPER_X, "X"); return 0; }
    if (!strcmp(t, "y") || !strcmp(t, "Y")) { home_one(DRV_STEPPER_Y, "Y"); return 0; }
    if (!strcmp(t, "z") || !strcmp(t, "Z")) { home_one(DRV_STEPPER_Z, "Z"); return 0; }
    Shell_Print("usage: home [x|y|z|all]\r\n");
    return -1;
}

static const ShellOpt_t s_homeOptions[] = {
    OPT("", "", "[x|y|z|all]", "Home axis/axes to min endstop and zero position", shell_home),
    OPT_END()
};

static const ShellModule_t s_homeModule = {
    "home", "Home axes to limit switches", MOD_CAT_SYSTEM,
    s_homeOptions, OPT_COUNT(s_homeOptions)
};

int MotionControl_Init(void)
{
    if (!Shell_RegisterModule(&s_motionModule)) return -1;
    if (!Shell_RegisterModule(&s_homeModule)) return -1;
    Shell_Print("motion control ready\r\n");
    return 0;
}

BANUX_COMPONENT_DEFINE_EX(g_banux_component_motion_control,
                          "motion_control", "1.0.0",
                          BANUX_COMPONENT_APPLICATION, 1,
                          "Trapezoidal motion planner (accel/decel)",
                          MotionControl_Init, NULL);
