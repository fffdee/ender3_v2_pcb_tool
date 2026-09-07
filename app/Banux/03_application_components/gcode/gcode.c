#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gcode.h"
#include "banux_config.h"
#include "banux_component.h"
#include "banux_io.h"
#include "bg_event.h"
#include "bg_shell.h"
#include "drv_stepper.h"
#include "motion_control.h"  /* MotionControl_Home：G28 回零复用已有实现 */
#include "app_bl.h"   /* app_bl_poll：阻塞执行期主动泵 UART 环形缓冲到 Shell 镜像 */

#define GCODE_LINE_MAX       96u
#define GCODE_DEFAULT_FEED   1200000L
#define GCODE_GROUP_PATH     "/driver/gpio/stepper_group"
#define GCODE_AMAX_STEPS_PER_S2 40000u  /* 联动加速度 steps/s^2（梯形加减速，先统一默认，实测后按轴调） */

/* ── 圆弧插补(G02/G03) ──────────────────────────────────────────────────
 * 约束：Keil 默认不带数学库，工程刻意不链接 libm（见 drv_stepper.c 的 stepper_dsqrt
 * 注释）。因此圆弧点生成采用「向量二分」：对单位向量 a、b 反复取 normalize(a+b) 得到
 * 角平分方向，递归产生 2^depth 段——只需开平方，不需要 sin/cos/atan2。
 * 段数按弦长逼近弧长的级数估算，保证每段弦长不超过 ARC_SEG_MM。 */
#define GCODE_ARC_SEG_MM       1000L   /* 单段目标弦长(毫毫米 = 1mm) */
#define GCODE_ARC_MAX_DEPTH    7u      /* 最多 2^7 = 128 段 */
#define GCODE_ARC_MIN_DEPTH    1u      /* 最少 2 段 */
#define GCODE_MM_PER_INCH_NUM  254L    /* 英寸→毫米：milli * 254 / 10 */
#define GCODE_MM_PER_INCH_DEN  10L
#define GCODE_PI               3.14159265358979

/* 执行期控制字符（上位机按钮发送）：阻塞期间 shell 不解析命令，只按原始字节识别。
 * 三者均非 ENTER_BOOT 帧头(0xAA)、也非 "boot" 文本，不会误触发 app_bl 升级复位。 */
#define GCODE_CTRL_STOP     ((uint8_t)'!')  /* 停止（中止执行、停脉冲、断使能） */
#define GCODE_CTRL_PAUSE    ((uint8_t)'%')  /* 暂停（停在段边界，喷嘴已抬到安全 Z） */
#define GCODE_CTRL_RESUME   ((uint8_t)'~')  /* 恢复 */
#define GCODE_ACT_STOP      1
#define GCODE_ACT_PAUSE     2
#define GCODE_ACT_RESUME    4

typedef struct {
    int codeType;
    int code;
    int32_t value[DRV_STEPPER_COUNT];
    uint8_t hasAxis[DRV_STEPPER_COUNT];
    int32_t feed;
    uint8_t hasFeed;
    int32_t ijk[3];      /* I/J/K：圆心相对起点的偏移(仅 G02/G03) */
    uint8_t hasIJK[3];
    int32_t radius;      /* R：圆弧半径(替代 I/J/K) */
    uint8_t hasRadius;
    int32_t paramS;      /* S：温度/主轴转速/风扇功率 */
    uint8_t hasS;
    int32_t paramP;      /* P：参数(M104 的工具号等) */
    uint8_t hasP;
    int32_t paramT;      /* T：刀具号(M06) */
    uint8_t hasT;
} ParsedGcode_t;

static GcodeState_t s_state;
static const int32_t s_stepsPerMm[DRV_STEPPER_COUNT] = {
    GCODE_X_STEPS_PER_MM, GCODE_Y_STEPS_PER_MM,
    GCODE_Z_STEPS_PER_MM, GCODE_E_STEPS_PER_MM
};
static const char *const s_axisPaths[DRV_STEPPER_COUNT] = {
    "/driver/gpio/stepper_x", "/driver/gpio/stepper_y",
    "/driver/gpio/stepper_z", "/driver/gpio/stepper_e"
};
static uint32_t s_segIdx = 0u;

/* 泵入 UART 环形缓冲并读取原始字节，返回检测到的控制动作位（STOP/PAUSE/RESUME）。
 * 关键：Gcode_ExecuteFile 是阻塞执行，期间主循环 Banux_Process 停摆、app_bl_poll 不再
 * 被调用，上位机发来的控制字符会滞留在 usart.c 环形缓冲里；而 Shell_RecvRaw 读的是
 * app_bl 的镜像缓冲（shell1/shell3），镜像只有在 app_bl_poll 搬运后才有数据 —— 这正是
 * “急停不管用”的根因。故此处每个段标记都先主动 app_bl_poll 把字节搬进镜像再读。 */
static int gcode_poll_control(void)
{
    uint8_t rbuf[16];
    uint16_t got;
    uint16_t i;
    int action = 0;

    app_bl_poll();
    got = Shell_RecvRaw(rbuf, sizeof(rbuf));
    for (i = 0u; i < got; i++) {
        if (rbuf[i] == GCODE_CTRL_STOP)         action |= GCODE_ACT_STOP;
        else if (rbuf[i] == GCODE_CTRL_PAUSE)   action |= GCODE_ACT_PAUSE;
        else if (rbuf[i] == GCODE_CTRL_RESUME)  action |= GCODE_ACT_RESUME;
    }
    return action;
}

/* 停脉冲并断使能（停止/中止共用）。 */
static void gcode_halt(void)
{
    (void)banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_STOP, NULL);
    s_state.motorsEnabled = 0u;
}

static int32_t div_round64(int64_t numerator, int32_t denominator)
{
    if (numerator >= 0) return (int32_t)((numerator + denominator / 2) / denominator);
    return (int32_t)((numerator - denominator / 2) / denominator);
}

static int parse_fixed(const char **cursor, int32_t *value)
{
    const char *p = *cursor;
    int sign = 1;
    int digits = 0;
    int decimals = 0;
    int32_t whole = 0;
    int32_t fraction = 0;

    if (*p == '+' || *p == '-') {
        if (*p++ == '-') sign = -1;
    }
    while (*p >= '0' && *p <= '9') {
        digits++;
        if (whole > 200000L) return -1;
        whole = whole * 10 + (*p++ - '0');
    }
    if (whole > 200000L) return -1;
    if (*p == '.') {
        p++;
        while (*p >= '0' && *p <= '9') {
            if (decimals < 3) fraction = fraction * 10 + (*p - '0');
            decimals++;
            digits++;
            p++;
        }
    }
    if (!digits) return -1;
    while (decimals < 3) {
        fraction *= 10;
        decimals++;
    }
    *value = sign * (whole * 1000L + fraction);
    *cursor = p;
    return 0;
}

static int verify_checksum(const char *line)
{
    const char *star = strchr(line, '*');
    const char *p;
    unsigned int checksum = 0u;
    int supplied = 0;

    if (!star) return 0;
    for (p = line; p < star; p++) checksum ^= (uint8_t)*p;
    p = star + 1;
    if (!isdigit((unsigned char)*p)) return -1;
    while (isdigit((unsigned char)*p)) supplied = supplied * 10 + (*p++ - '0');
    return checksum == (unsigned int)supplied ? 0 : -1;
}

static int parse_line(const char *line, ParsedGcode_t *parsed)
{
    const char *p = line;

    memset(parsed, 0, sizeof(*parsed));
    parsed->codeType = 0;
    if (verify_checksum(line) != 0) return GCODE_ERR_CHECKSUM;

    while (*p) {
        int letter;
        int32_t value;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == ';' || *p == '*') break;
        if (*p == '(') {
            while (*p && *p != ')') p++;
            if (*p == ')') p++;
            continue;
        }
        if (!isalpha((unsigned char)*p)) return GCODE_ERR_INVALID;
        letter = toupper((unsigned char)*p++);
        if (parse_fixed(&p, &value) != 0) return GCODE_ERR_INVALID;
        switch (letter) {
            case 'N': break;
            case 'G':
            case 'M':
                if (parsed->codeType) return GCODE_ERR_INVALID;
                if ((value % 1000L) != 0) return GCODE_ERR_UNSUPPORTED;
                parsed->codeType = letter;
                parsed->code = (int)(value / 1000L);
                break;
            case 'X': parsed->hasAxis[0] = 1u; parsed->value[0] = value; break;
            case 'Y': parsed->hasAxis[1] = 1u; parsed->value[1] = value; break;
            case 'Z': parsed->hasAxis[2] = 1u; parsed->value[2] = value; break;
            case 'E': parsed->hasAxis[3] = 1u; parsed->value[3] = value; break;
            case 'F': parsed->hasFeed = 1u; parsed->feed = value; break;
            /* 圆弧圆心偏移(I/J/K)与半径(R)；S/P/T 为温度/转速/风扇/刀具等参数。
             * 这些字母若走到 default 会判为 UNKNOWN_WORD 而整行失败，故必须放行，
             * 具体语义由各指令自行解释。 */
            case 'I': parsed->hasIJK[0] = 1u; parsed->ijk[0] = value; break;
            case 'J': parsed->hasIJK[1] = 1u; parsed->ijk[1] = value; break;
            case 'K': parsed->hasIJK[2] = 1u; parsed->ijk[2] = value; break;
            case 'R': parsed->hasRadius = 1u; parsed->radius = value; break;
            case 'S': parsed->hasS = 1u; parsed->paramS = value; break;
            case 'P': parsed->hasP = 1u; parsed->paramP = value; break;
            case 'T': parsed->hasT = 1u; parsed->paramT = value; break;
            default: return GCODE_ERR_UNKNOWN_WORD;
        }
    }
    return GCODE_OK;
}

static int refresh_position(void)
{
    uint32_t axis;
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        DrvStepperStatus_t status;
        if (banux_read(s_axisPaths[axis], &status, sizeof(status)) < 0) return -1;
        s_state.positionMilliMm[axis] = div_round64((int64_t)status.position * 1000L,
                                                    s_stepsPerMm[axis]);
    }
    return 0;
}

/* 单位换算：解析器给出的数值单位是「毫-当前单位」；G20(英制)下需 ×25.4 得到毫毫米。 */
static int32_t to_milli_mm(int32_t rawMilli)
{
    if (!s_state.unitsInch) return rawMilli;
    return div_round64((int64_t)rawMilli * GCODE_MM_PER_INCH_NUM,
                       GCODE_MM_PER_INCH_DEN);
}

/* 自实现开平方(牛顿迭代)。工程刻意不链接 libm（见 drv_stepper.c 的 stepper_dsqrt），
 * 本组件自持一份，与 motion_control 的 dsqrt 同思路，避免反向依赖。 */
static double gcode_dsqrt(double x)
{
    double g = 1.0;
    int i;
    if (x <= 0.0) return 0.0;
    for (i = 0; i < 16; i++) {
        g = 0.5 * (g + x / g);
    }
    return g;
}

/* 移动到绝对目标坐标(毫毫米)。直线与圆弧的每一段最终都走这里。 */
static int move_to(const int32_t targetMilliMm[DRV_STEPPER_COUNT])
{
    DrvStepperMoveCommand_t command;
    int32_t maxDistance = 0;
    uint32_t maxSteps = 0u;
    uint32_t axis;
    uint64_t durationUs;

    memset(&command, 0, sizeof(command));
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        int32_t delta = targetMilliMm[axis] - s_state.positionMilliMm[axis];
        uint32_t count;
        command.steps[axis] = div_round64((int64_t)delta * s_stepsPerMm[axis], 1000L);
        count = (uint32_t)(command.steps[axis] < 0
                        ? -command.steps[axis] : command.steps[axis]);
        if (count > maxSteps) maxSteps = count;
        if (delta < 0) delta = -delta;
        if (delta > maxDistance) maxDistance = delta;
    }
    if (!maxSteps) return GCODE_OK;
    /* 由 feed 与主轴位移估算主轴最大速度(steps/s)：durationUs = maxDistance*60e6/feed(milli)，
     * vMax = maxSteps*1e6/durationUs。加速度用统一默认；驱动侧 stepper_move_group 据此做
     * 梯形加减速多轴联动（不再受 5 秒时长上限约束，长移动不会被拒为 -5）。 */
    durationUs = ((uint64_t)(uint32_t)maxDistance * 60000000ULL) /
                 (uint32_t)s_state.feedMilliMmPerMin;
    if (durationUs == 0u) durationUs = 1u;
    command.vMaxStepsPerSec = (uint32_t)(((uint64_t)maxSteps * 1000000ULL) / durationUs);
    command.aMaxStepsPerSec2 = GCODE_AMAX_STEPS_PER_S2;
    command.pulseUs = 0u;
    if (banux_write(GCODE_GROUP_PATH, &command, sizeof(command)) < 0) {
        return GCODE_ERR_DRIVER;
    }
    return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
}

/* 由解析结果算出终点绝对坐标(毫毫米)：处理 G90/G91 绝对/相对 与 G20/G21 单位。 */
static void compute_linear_target(const ParsedGcode_t *parsed,
                                  int32_t target[DRV_STEPPER_COUNT])
{
    uint32_t axis;
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        if (parsed->hasAxis[axis]) {
            int32_t v = to_milli_mm(parsed->value[axis]);
            target[axis] = s_state.absoluteMode
                         ? v : s_state.positionMilliMm[axis] + v;
        } else {
            target[axis] = s_state.positionMilliMm[axis];
        }
    }
}

static int execute_move(const ParsedGcode_t *parsed)
{
    int32_t target[DRV_STEPPER_COUNT];

    if (parsed->hasFeed) {
        int32_t feed = to_milli_mm(parsed->feed);
        if (feed <= 0) return GCODE_ERR_INVALID;
        s_state.feedMilliMmPerMin = feed;
    }
    compute_linear_target(parsed, target);
    return move_to(target);
}

/* ══ 圆弧插补(G02/G03) ═══════════════════════════════════════════════════
 * 用「向量二分」生成圆弧点：对相对圆心的单位向量 a、b 反复取 normalize(a+b) 得到
 * 角平分方向，递归出 2^depth 段。全程只需开平方，不需要 sin/cos/atan2，
 * 因此不必链接 libm（工程刻意不依赖数学库）。 */

typedef struct { double x; double y; } GcodeVec2_t;

/* 圆弧点生成期间的段计数与第三轴(螺旋)插值状态。执行单线程，用静态量避免层层传参。 */
static uint32_t s_arcSegNo;
static uint32_t s_arcSegTotal;
static uint32_t s_arcThirdAxis;   /* == DRV_STEPPER_COUNT 表示平面外第三轴不参与 */
static int32_t  s_arcThirdFrom;
static int32_t  s_arcThirdTo;

/* 求 a→b 圆弧的中点方向(单位向量)。
 * 常规取 normalize(a+b)；但当弧接近/等于 180° 时 a+b 趋近零向量，归一化结果不可靠，
 * 此时改用「垂直于 a 的方向」——它正是弧的 90° 处，方向由旋向决定：
 *   逆时针(CCW)取左法向 (-a.y,  a.x)，顺时针(CW)取右法向 (a.y, -a.x)。
 * negate=1 时取反，用于大弧(>180°)：此时 a+b 指向小弧一侧，真正的中点在反向。 */
static GcodeVec2_t arc_mid(GcodeVec2_t a, GcodeVec2_t b, int cw, int negate)
{
    GcodeVec2_t sum;
    GcodeVec2_t m;
    double len;

    sum.x = a.x + b.x;
    sum.y = a.y + b.y;
    len = gcode_dsqrt(sum.x * sum.x + sum.y * sum.y);
    if (len < 1e-6) {
        if (cw) { m.x =  a.y; m.y = -a.x; }
        else    { m.x = -a.y; m.y =  a.x; }
        return m;
    }
    m.x = sum.x / len;
    m.y = sum.y / len;
    if (negate) { m.x = -m.x; m.y = -m.y; }
    return m;
}

/* 递归二分：depth=0 时移动到 b 对应的实际点。negate 仅由顶层大弧拆分时置 1。 */
static int arc_emit(const uint32_t axisIdx[2], const double center[2],
                    double radius, GcodeVec2_t a, GcodeVec2_t b,
                    uint32_t depth, int cw, int negate)
{
    int32_t target[DRV_STEPPER_COUNT];
    uint32_t axis;
    int result;
    GcodeVec2_t m;

    if (depth > 0u) {
        m = arc_mid(a, b, cw, negate);
        result = arc_emit(axisIdx, center, radius, a, m, depth - 1u, cw, 0);
        if (result != GCODE_OK) return result;
        return arc_emit(axisIdx, center, radius, m, b, depth - 1u, cw, 0);
    }
    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        target[axis] = s_state.positionMilliMm[axis];
    }
    target[axisIdx[0]] = (int32_t)(center[0] + b.x * radius);
    target[axisIdx[1]] = (int32_t)(center[1] + b.y * radius);
    /* 平面外的第三轴沿弧长线性插值 → 支持螺旋插补 */
    if (s_arcThirdAxis < DRV_STEPPER_COUNT && s_arcSegTotal > 0u) {
        if (s_arcSegNo < s_arcSegTotal) s_arcSegNo++;
        target[s_arcThirdAxis] = s_arcThirdFrom +
            div_round64((int64_t)(s_arcThirdTo - s_arcThirdFrom) * (int64_t)s_arcSegNo,
                        (int32_t)s_arcSegTotal);
    }
    return move_to(target);
}

static int execute_arc(const ParsedGcode_t *parsed, int cw)
{
    uint32_t axisIdx[2];
    uint32_t thirdAxis;
    int32_t target[DRV_STEPPER_COUNT];
    int32_t startMm[2];
    int32_t endMm[2];
    int32_t off0, off1;
    double center[2];
    double u0x, u0y, u1x, u1y;
    double r0, r1, radius, radiusDiff;
    double dx, dy, chord, ratio, sweep, arcLen, cross;
    double rReq, midX, midY, half, dist, nx, ny, sgn;
    uint32_t depth, seg;
    int major;
    int result;
    GcodeVec2_t a, b, m, opp;

    if (parsed->hasFeed) {
        int32_t feed = to_milli_mm(parsed->feed);
        if (feed <= 0) return GCODE_ERR_INVALID;
        s_state.feedMilliMmPerMin = feed;
    }
    compute_linear_target(parsed, target);

    if (s_state.plane == GCODE_PLANE_XY) {
        axisIdx[0] = 0u; axisIdx[1] = 1u; thirdAxis = 2u;
    } else if (s_state.plane == GCODE_PLANE_XZ) {
        axisIdx[0] = 0u; axisIdx[1] = 2u; thirdAxis = 1u;
    } else {
        axisIdx[0] = 1u; axisIdx[1] = 2u; thirdAxis = 0u;
    }
    startMm[0] = s_state.positionMilliMm[axisIdx[0]];
    startMm[1] = s_state.positionMilliMm[axisIdx[1]];
    endMm[0]   = target[axisIdx[0]];
    endMm[1]   = target[axisIdx[1]];

    off0 = 0;
    off1 = 0;
    if (parsed->hasIJK[0] || parsed->hasIJK[1] || parsed->hasIJK[2]) {
        /* I/J/K → 平面两轴的映射：XY 用 (I,J)，XZ 用 (I,K)，YZ 用 (J,K) */
        if (s_state.plane == GCODE_PLANE_XY) {
            if (parsed->hasIJK[0]) off0 = to_milli_mm(parsed->ijk[0]);
            if (parsed->hasIJK[1]) off1 = to_milli_mm(parsed->ijk[1]);
        } else if (s_state.plane == GCODE_PLANE_XZ) {
            if (parsed->hasIJK[0]) off0 = to_milli_mm(parsed->ijk[0]);
            if (parsed->hasIJK[2]) off1 = to_milli_mm(parsed->ijk[2]);
        } else {
            if (parsed->hasIJK[1]) off0 = to_milli_mm(parsed->ijk[1]);
            if (parsed->hasIJK[2]) off1 = to_milli_mm(parsed->ijk[2]);
        }
        center[0] = (double)startMm[0] + (double)off0;
        center[1] = (double)startMm[1] + (double)off1;
    } else if (parsed->hasRadius) {
        /* R 形式：圆心 = 弦中点 ± 垂直偏移。R 取负表示走大弧。 */
        rReq = (double)to_milli_mm(parsed->radius);
        sgn = cw ? -1.0 : 1.0;
        if (rReq < 0.0) { rReq = -rReq; sgn = -sgn; }
        dx   = (double)endMm[0] - (double)startMm[0];
        dy   = (double)endMm[1] - (double)startMm[1];
        dist = gcode_dsqrt(dx * dx + dy * dy);
        if (dist < 1e-9) return GCODE_ERR_INVALID;         /* 起终点重合，无解 */
        if (rReq < dist * 0.5) return GCODE_ERR_INVALID;   /* 半径小于弦长一半，无解 */
        half = gcode_dsqrt(rReq * rReq - (dist * 0.5) * (dist * 0.5));
        midX = ((double)startMm[0] + (double)endMm[0]) * 0.5;
        midY = ((double)startMm[1] + (double)endMm[1]) * 0.5;
        nx = -dy / dist;   /* 左法向 */
        ny =  dx / dist;
        center[0] = midX + sgn * half * nx;
        center[1] = midY + sgn * half * ny;
    } else {
        return GCODE_ERR_INVALID;   /* 既无 I/J/K 也无 R，无法定义圆弧 */
    }

    u0x = (double)startMm[0] - center[0];
    u0y = (double)startMm[1] - center[1];
    u1x = (double)endMm[0]   - center[0];
    u1y = (double)endMm[1]   - center[1];
    r0 = gcode_dsqrt(u0x * u0x + u0y * u0y);
    r1 = gcode_dsqrt(u1x * u1x + u1y * u1y);
    if (r0 < 1e-6 || r1 < 1e-6) return GCODE_ERR_INVALID;
    radius = (r0 + r1) * 0.5;
    /* 起终点半径应一致：差异过大说明终点不在这个圆上（容忍 5% + 0.05mm） */
    radiusDiff = r0 - r1;
    if (radiusDiff < 0.0) radiusDiff = -radiusDiff;
    if (radiusDiff > radius * 0.05 + 50.0) return GCODE_ERR_INVALID;

    /* 由弦长反推圆心角，避开 asin：sweep = 2*asin(ratio)，
     * asin(z) ≈ z + z^3/6 + 3z^5/40  ⇒  sweep ≈ 2z + z^3/3 + 3z^5/20 */
    dx    = u1x - u0x;
    dy    = u1y - u0y;
    chord = gcode_dsqrt(dx * dx + dy * dy);
    if (chord < 1e-6) {
        sweep = 2.0 * GCODE_PI;   /* 起点与终点重合 → 整圆 */
        major = 0;
    } else {
        ratio = chord / (2.0 * radius);
        if (ratio > 1.0) ratio = 1.0;
        sweep = 2.0 * ratio + (ratio * ratio * ratio) / 3.0
              + 3.0 * ratio * ratio * ratio * ratio * ratio / 20.0;
        /* 走小弧还是大弧：由 CW/CCW 与起终点叉积符号决定 */
        cross = u0x * u1y - u0y * u1x;
        if (cw) major = (cross > 0.0) ? 1 : 0;
        else    major = (cross < 0.0) ? 1 : 0;
        if (major) sweep = 2.0 * GCODE_PI - sweep;
    }

    arcLen = radius * sweep;
    seg = (uint32_t)(arcLen / (double)GCODE_ARC_SEG_MM);
    if (seg < 2u) seg = 2u;
    depth = GCODE_ARC_MIN_DEPTH;
    while ((((uint32_t)1u) << depth) < seg && depth < GCODE_ARC_MAX_DEPTH) {
        depth++;
    }

    s_arcSegTotal  = ((uint32_t)1u) << depth;
    s_arcSegNo     = 0u;
    s_arcThirdFrom = s_state.positionMilliMm[thirdAxis];
    s_arcThirdTo   = target[thirdAxis];
    s_arcThirdAxis = (s_arcThirdTo != s_arcThirdFrom)
                   ? thirdAxis : (uint32_t)DRV_STEPPER_COUNT;

    a.x = u0x / radius; a.y = u0y / radius;
    b.x = u1x / radius; b.y = u1y / radius;
    if (chord < 1e-6) {
        /* 整圆：a 与 b 重合，必须拆成两个半圆输出，否则二分法只会原地不动 */
        opp.x = -a.x;
        opp.y = -a.y;
        result = arc_emit(axisIdx, center, radius, a, opp, depth - 1u, cw, 0);
        if (result != GCODE_OK) return result;
        result = arc_emit(axisIdx, center, radius, opp, a, depth - 1u, cw, 0);
    } else if (sweep > GCODE_PI) {
        /* 大弧(>180°)：a+b 指向小弧一侧，顶层中点须取反；之后各子弧均 ≤ 180° */
        m = arc_mid(a, b, cw, 1);
        result = arc_emit(axisIdx, center, radius, a, m, depth - 1u, cw, 0);
        if (result != GCODE_OK) return result;
        result = arc_emit(axisIdx, center, radius, m, b, depth - 1u, cw, 0);
    } else {
        result = arc_emit(axisIdx, center, radius, a, b, depth, cw, 0);
    }
    if (result != GCODE_OK) return result;
    /* 收尾：精确落到目标点，消除半径取整与递归累积误差，并确保第三轴到位 */
    return move_to(target);
}

/* G28 回零：复用 motion_control 已有的「快速逼近 + 退开 + 二次慢速逼近」实现。
 * 未指定任何轴则依次回 X/Y/Z；E 轴没有限位开关，不参与回零。 */
static int execute_home(const ParsedGcode_t *parsed)
{
    static const DrvStepperAxis_t homeOrder[3] = {
        DRV_STEPPER_X, DRV_STEPPER_Y, DRV_STEPPER_Z
    };
    uint32_t i;
    int anyAxis = 0;
    int enabled = 1;
    int result;

    for (i = 0u; i < 3u; i++) {
        if (parsed->hasAxis[i]) { anyAxis = 1; break; }
    }
    /* 回零前必须先使能，否则 MotionControl_Home 直接返回 -2 */
    if (banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_ENABLE, &enabled) != 0) {
        return GCODE_ERR_DRIVER;
    }
    s_state.motorsEnabled = 1u;
    for (i = 0u; i < 3u; i++) {
        if (anyAxis && !parsed->hasAxis[i]) continue;
        result = MotionControl_Home(homeOrder[i]);
        if (result != 0) {
            Shell_Printf("gcode: home %c failed (%d)\r\n", (int)('X' + (int)i), result);
            return GCODE_ERR_DRIVER;
        }
    }
    return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
}

/* 无对应硬件的 M 代码：接受并忽略，仅首次告警。
 * 本机(Ender3 V2 改点胶机)没有加热器、风扇、主轴、刀库，这些指令无法产生实际动作。
 * 但通用上位机/切片软件生成的 gcode 常包含这些行，若判为 UNSUPPORTED 会让
 * Gcode_ExecuteFile 直接中止整个文件，故按 Marlin 惯例接受并忽略。 */
static void warn_no_hardware_once(int code)
{
    static const int knownCodes[8] = { 3, 4, 5, 6, 104, 106, 107, 140 };
    static uint8_t warned[8];
    int i;
    for (i = 0; i < 8; i++) {
        if (knownCodes[i] == code) {
            if (!warned[i]) {
                warned[i] = 1u;
                Shell_Printf("gcode: M%d accepted, no hardware (ignored)\r\n", code);
            }
            return;
        }
    }
}

static int execute_parsed(const ParsedGcode_t *parsed)
{
    uint32_t axis;
    int enabled;

    /* 纯坐标行(无 G/M)：G 代码是模态的，必须沿用上一次的运动模式。
     * 原来直接 return OK 会把 "X10 Y20" 这类行静默丢弃。 */
    if (!parsed->codeType) {
        switch (s_state.motionMode) {
            case GCODE_MOTION_CW:  return execute_arc(parsed, 1);
            case GCODE_MOTION_CCW: return execute_arc(parsed, 0);
            default:               return execute_move(parsed);
        }
    }
    if (parsed->codeType == 'G') {
        switch (parsed->code) {
            case 0:
            case 1:
                s_state.motionMode = GCODE_MOTION_LINEAR;
                return execute_move(parsed);
            case 2:
                s_state.motionMode = GCODE_MOTION_CW;
                return execute_arc(parsed, 1);   /* G02 顺时针 */
            case 3:
                s_state.motionMode = GCODE_MOTION_CCW;
                return execute_arc(parsed, 0);   /* G03 逆时针 */
            case 17: s_state.plane = GCODE_PLANE_XY; return GCODE_OK;
            case 18: s_state.plane = GCODE_PLANE_XZ; return GCODE_OK;
            case 19: s_state.plane = GCODE_PLANE_YZ; return GCODE_OK;
            case 20: s_state.unitsInch = 1u; return GCODE_OK;   /* 英制 */
            case 21: s_state.unitsInch = 0u; return GCODE_OK;   /* 公制 */
            case 28: return execute_home(parsed);
            case 90: s_state.absoluteMode = 1u; return GCODE_OK;
            case 91: s_state.absoluteMode = 0u; return GCODE_OK;
            case 92:
                for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
                    if (parsed->hasAxis[axis]) {
                        int32_t steps = div_round64((int64_t)to_milli_mm(parsed->value[axis]) *
                                                    s_stepsPerMm[axis], 1000L);
                        if (banux_ioctl(s_axisPaths[axis],
                                        DRV_STEPPER_IOCTL_POSITION, &steps) != 0) {
                            return GCODE_ERR_DRIVER;
                        }
                    }
                }
                return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
            default: return GCODE_ERR_UNSUPPORTED;
        }
    }
    switch (parsed->code) {
        case 2:    /* 程序结束 */
        case 30:
            gcode_halt();
            return GCODE_OK;
        case 3:    /* 主轴正转 */
        case 4:    /* 主轴反转 */
        case 5:    /* 主轴停止 */
        case 6:    /* 自动换刀 */
        case 104:  /* 挤出机温度 */
        case 106:  /* 风扇开 */
        case 107:  /* 风扇关 */
        case 140:  /* 热床温度 */
            warn_no_hardware_once(parsed->code);
            return GCODE_OK;
        case 17:
            enabled = 1;
            if (banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_ENABLE, &enabled) != 0)
                return GCODE_ERR_DRIVER;
            s_state.motorsEnabled = 1u;
            return GCODE_OK;
        case 18:
        case 84:
            enabled = 0;
            if (banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_ENABLE, &enabled) != 0)
                return GCODE_ERR_DRIVER;
            s_state.motorsEnabled = 0u;
            return GCODE_OK;
        case 114:
            return refresh_position() == 0 ? GCODE_OK : GCODE_ERR_DRIVER;
        default: return GCODE_ERR_UNSUPPORTED;
    }
}

int Gcode_ExecuteLine(const char *line)
{
    ParsedGcode_t parsed;
    int result;

    if (!line) return GCODE_ERR_INVALID;
    BG_EVT_PUB_DATA(EVT_GCODE_COMMAND, line,
                    strlen(line) > 255u ? 255u : (uint8_t)strlen(line));
    result = parse_line(line, &parsed);
    if (result == GCODE_OK) result = execute_parsed(&parsed);
    if (result == GCODE_OK) BG_EVT_PUB(EVT_GCODE_COMPLETE);
    else BG_EVT_PUB_DATA(EVT_GCODE_ERROR, &result, sizeof(result));
    return result;
}

int Gcode_ExecuteFile(const char *path, int reportProgress)
{
    char chunk[64];
    char line[GCODE_LINE_MAX];
    uint32_t offset = 0u;
    uint32_t lineNumber = 1u;
    uint16_t lineLength = 0u;
    int count;
    int i;

    if (!path) return GCODE_ERR_INVALID;
    if (reportProgress) {
        s_segIdx = 0u;
        (void)refresh_position();
        Shell_Printf("@PG 0 %ld %ld %ld\r\n", (long)s_state.positionMilliMm[0],
                     (long)s_state.positionMilliMm[1], (long)s_state.positionMilliMm[2]);
    }
    for (;;) {
        count = banux_read_at(path, chunk, sizeof(chunk), offset);
        if (count < 0) return GCODE_ERR_DRIVER;
        if (count == 0) break;
        offset += (uint32_t)count;
        for (i = 0; i < count; i++) {
            unsigned char c = (unsigned char)chunk[i];
            if (offset - (uint32_t)count + (uint32_t)i < 3u &&
                ((offset - (uint32_t)count + (uint32_t)i == 0u && c == 0xEFu) ||
                 (offset - (uint32_t)count + (uint32_t)i == 1u && c == 0xBBu) ||
                 (offset - (uint32_t)count + (uint32_t)i == 2u && c == 0xBFu))) {
                continue;
            }
            if (c == '\r') continue;
            if (c == '\n') {
                if (lineLength) {
                    int result;
                    line[lineLength] = '\0';
                    /* 段标记 ;@SEG：此处上一段运动已阻塞走完、位置已刷新，坐标为真实平面位置，
                     * 且喷嘴已抬到安全 Z —— 是检测停止/暂停的最安全时机。先泵入并轮询控制字符，
                     * 再回报进度（走 shell 输出行，串口直连与无线透传都通用）。 */
                    if (reportProgress && strncmp(line, ";@SEG", 5) == 0) {
                        int action = gcode_poll_control();
                        if (action & GCODE_ACT_STOP) {
                            gcode_halt();
                            Shell_Print("gcode: aborted\r\n");
                            return GCODE_ERR_ABORTED;
                        }
                        if (action & GCODE_ACT_PAUSE) {
                            /* 暂停：保持电机使能以锁定位置，持续泵入轮询直到恢复('~')或停止('!')。
                             * 期间不回报 @PG；上位机侧相应挂起无响应看门狗。 */
                            Shell_Print("gcode: paused\r\n");
                            for (;;) {
                                int held = gcode_poll_control();
                                if (held & GCODE_ACT_STOP) {
                                    gcode_halt();
                                    Shell_Print("gcode: aborted\r\n");
                                    return GCODE_ERR_ABORTED;
                                }
                                if (held & GCODE_ACT_RESUME) {
                                    Shell_Print("gcode: resumed\r\n");
                                    break;
                                }
                                HAL_Delay(20);
                            }
                        }
                        s_segIdx++;
                        Shell_Printf("@PG %lu %ld %ld %ld\r\n", (unsigned long)s_segIdx,
                                     (long)s_state.positionMilliMm[0],
                                     (long)s_state.positionMilliMm[1],
                                     (long)s_state.positionMilliMm[2]);
                    }
                    result = Gcode_ExecuteLine(line);
                    if (result != GCODE_OK) {
                        Shell_Printf("gcode: line %lu failed (%d)\r\n",
                                     (unsigned long)lineNumber, result);
                        return result;
                    }
                    lineLength = 0u;
                }
                lineNumber++;
            } else {
                if (lineLength + 1u >= sizeof(line)) return GCODE_ERR_LINE_TOO_LONG;
                line[lineLength++] = (char)c;
            }
        }
    }
    if (lineLength) {
        int result;
        line[lineLength] = '\0';
        result = Gcode_ExecuteLine(line);
        if (result != GCODE_OK) return result;
    }
    return GCODE_OK;
}

void Gcode_GetState(GcodeState_t *state)
{
    if (state) *state = s_state;
}

static void print_state(void)
{
    GcodeState_t state;
    char value[5][20];
    uint32_t i;
    Gcode_GetState(&state);
    for (i = 0u; i < 5u; i++) {
        int32_t milli = i < 4u ? state.positionMilliMm[i]
                               : state.feedMilliMmPerMin;
        uint32_t magnitude = (uint32_t)(milli < 0 ? -(int64_t)milli : milli);
        snprintf(value[i], sizeof(value[i]), "%s%lu.%03lu",
                 milli < 0 ? "-" : "", (unsigned long)(magnitude / 1000u),
                 (unsigned long)(magnitude % 1000u));
    }
    {
        const char *planeName = (state.plane == GCODE_PLANE_XY) ? "XY"
                              : (state.plane == GCODE_PLANE_XZ) ? "XZ" : "YZ";
        Shell_Printf("X:%s Y:%s Z:%s E:%s F:%s mode=%s units=%s plane=%s motors=%s\r\n",
                     value[0], value[1], value[2], value[3], value[4],
                     state.absoluteMode ? "absolute" : "relative",
                     state.unitsInch ? "inch" : "mm", planeName,
                     state.motorsEnabled ? "on" : "off");
    }
}

static int shell_gcode_line(int argc, char *argv[])
{
    char line[GCODE_LINE_MAX];
    int i;
    int result;
    size_t length = 0u;

    if (argc <= 0) return GCODE_ERR_INVALID;
    line[0] = '\0';
    for (i = 0; i < argc; i++) {
        size_t part = strlen(argv[i]);
        if (length + part + (i ? 1u : 0u) >= sizeof(line)) return GCODE_ERR_LINE_TOO_LONG;
        if (i) line[length++] = ' ';
        memcpy(&line[length], argv[i], part);
        length += part;
        line[length] = '\0';
    }
    result = Gcode_ExecuteLine(line);
    if (result == GCODE_OK) {
        if ((line[0] == 'M' || line[0] == 'm') && atoi(&line[1]) == 114) print_state();
        else Shell_Print("ok\r\n");
    } else {
        Shell_Printf("gcode: rejected (%d)\r\n", result);
    }
    return result;
}

static int shell_gcode_file(int argc, char *argv[])
{
    int result;
    int reportProgress = 0;
    int i;
    if (argc < 1) return GCODE_ERR_INVALID;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) reportProgress = 1;
    }
    result = Gcode_ExecuteFile(argv[0], reportProgress);
    if (result == GCODE_OK) Shell_Print("gcode: file complete\r\n");
    return result;
}

static int shell_gcode_status(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    print_state();
    return 0;
}

static const ShellOpt_t s_gcodeOptions[] = {
    OPT("", "", "<line>", "Execute one G-code line", shell_gcode_line),
    OPT("f", "file", "<path>", "Execute a G-code file", shell_gcode_file),
    OPT("s", "status", NULL, "Show G-code state", shell_gcode_status),
    OPT_END()
};

static const ShellModule_t s_gcodeModule = {
    "gcode", "Parse and execute G-code", MOD_CAT_SYSTEM,
    s_gcodeOptions, OPT_COUNT(s_gcodeOptions)
};

static void on_gcode_stop(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    (void)topic;
    (void)data;
    (void)size;
    (void)banux_ioctl(GCODE_GROUP_PATH, DRV_STEPPER_IOCTL_STOP, NULL);
    s_state.motorsEnabled = 0u;
}

int Gcode_Init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    s_state.absoluteMode = 1u;                 /* G90 绝对坐标 */
    s_state.unitsInch    = 0u;                 /* G21 公制(mm) */
    s_state.plane        = GCODE_PLANE_XY;     /* G17 */
    s_state.motionMode   = GCODE_MOTION_LINEAR;/* G00/G01 直线 */
    s_state.feedMilliMmPerMin = GCODE_DEFAULT_FEED;
    if (refresh_position() != 0) return -1;
    if (!Shell_RegisterModule(&s_gcodeModule)) return -1;
    if (BG_Event_SubscribeNamed(EVT_GCODE_STOP, on_gcode_stop,
                                "gcode.emergency_stop") != 0) return -2;
    return 0;
}

BANUX_COMPONENT_DEFINE_EX(g_banux_component_gcode,
                          "gcode", "1.0.0",
                          BANUX_COMPONENT_APPLICATION, BANUX_GCODE_EN,
                          "G-code parser and stepper adapter",
                          Gcode_Init, NULL);
