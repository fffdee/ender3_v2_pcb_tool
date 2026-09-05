#include <stdio.h>
#include <string.h>
#include "drv_stepper.h"
#include "drv_device.h"
#include "debug.h"
#include "banux_config.h"
#include "stm32f1xx_hal.h"
#include "bg_event.h"
#include "banux_component.h"

#define STEPPER_EN_PORT       GPIOC
#define STEPPER_EN_PIN        GPIO_PIN_3
#define STEPPER_DEFAULT_US    500u
#define STEPPER_MIN_PULSE_US  2u
#define STEPPER_MAX_PULSE_US  100000u
#define STEPPER_MAX_CMD_STEPS 100000u
#define STEPPER_MAX_MOVE_US   5000000u

typedef struct {
    DrvStepperAxis_t axis;
    const char *axisName;
    GPIO_TypeDef *stepPort;
    uint16_t stepPin;
    GPIO_TypeDef *dirPort;
    uint16_t dirPin;
    GPIO_TypeDef *limitPort;
    uint16_t limitPin;
    const char *stepPinName;
    const char *dirPinName;
    const char *limitPinName;
    uint32_t pulseUs;
    int32_t position;
    uint8_t direction;
    uint8_t dirInvert;   /* 1=物理 DIR 引脚电平相对逻辑方向取反（见 STEPPER_INVERT_*_DIR） */
} StepperPriv_t;

static StepperPriv_t s_steppers[DRV_STEPPER_COUNT] = {
    { DRV_STEPPER_X, "X", GPIOC, GPIO_PIN_2, GPIOB, GPIO_PIN_9,
      GPIOA, GPIO_PIN_5, "PC2", "PB9", "PA5", STEPPER_DEFAULT_US, 0, 0, STEPPER_INVERT_X_DIR },
    { DRV_STEPPER_Y, "Y", GPIOB, GPIO_PIN_8, GPIOB, GPIO_PIN_7,
      GPIOA, GPIO_PIN_6, "PB8", "PB7", "PA6", STEPPER_DEFAULT_US, 0, 0, STEPPER_INVERT_Y_DIR },
    { DRV_STEPPER_Z, "Z", GPIOB, GPIO_PIN_6, GPIOB, GPIO_PIN_5,
      GPIOA, GPIO_PIN_7, "PB6", "PB5", "PA7", STEPPER_DEFAULT_US, 0, 0, STEPPER_INVERT_Z_DIR },
    { DRV_STEPPER_E, "E", GPIOB, GPIO_PIN_4, GPIOB, GPIO_PIN_3,
      NULL, 0, "PB4", "PB3", NULL, STEPPER_DEFAULT_US, 0, 0, STEPPER_INVERT_E_DIR },
};

static uint8_t s_gpioInitialized;
static uint8_t s_enabled;
static uint8_t s_busy;

static int stepper_axis_valid(DrvStepperAxis_t axis)
{
    return axis < DRV_STEPPER_COUNT;
}

static void stepper_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t cycles = (SystemCoreClock / 1000000u) * us;

    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}

static void stepper_gpio_init(void)
{
    GPIO_InitTypeDef init;

    if (s_gpioInitialized) return;

    __HAL_RCC_AFIO_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 |
                            GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 |
                            GPIO_PIN_9, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(STEPPER_EN_PORT, STEPPER_EN_PIN, GPIO_PIN_SET);

    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    init.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
               GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &init);
    init.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOC, &init);

    /* 限位输入：X=PA5 / Y=PA6 / Z=PA7
     * 板上限位自带 10K 上拉与 100nF 滤波，故配置为浮空输入。 */
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOA, &init);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    s_enabled = 0u;
    s_gpioInitialized = 1u;
}

int DrvStepper_EnableAll(int enabled)
{
    stepper_gpio_init();
    s_enabled = enabled ? 1u : 0u;
    HAL_GPIO_WritePin(STEPPER_EN_PORT, STEPPER_EN_PIN,
                      s_enabled ? GPIO_PIN_RESET : GPIO_PIN_SET);
    return 0;
}

int DrvStepper_SetDirection(DrvStepperAxis_t axis, int direction)
{
    StepperPriv_t *stepper;
    uint8_t pinDir;

    if (!stepper_axis_valid(axis)) return -1;
    stepper_gpio_init();
    stepper = &s_steppers[axis];
    stepper->direction = direction ? 1u : 0u;   /* 逻辑方向：决定 position 增减 */
    /* 物理 DIR 电平 = 逻辑方向 XOR 该轴翻转位（Z 翻转后：正命令=上升） */
    pinDir = (uint8_t)(stepper->direction ^ stepper->dirInvert);
    HAL_GPIO_WritePin(stepper->dirPort, stepper->dirPin,
                      pinDir ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
}

/* 返回 1=该轴 min 限位已触发；0=未触发或该轴无限位(如 E)。
 * 与 DrvStepper_GetLimit 同极性逻辑，供脉冲循环内联急停判定用。 */
static uint8_t stepper_limit_hit(const StepperPriv_t *st)
{
    int raw;
    if (!st->limitPort || st->limitPin == 0u) return 0u;
    raw = (HAL_GPIO_ReadPin(st->limitPort, st->limitPin) == GPIO_PIN_SET);
    return (uint8_t)(STEPPER_LIMIT_ACTIVE_HIGH ? raw : !raw);
}

int DrvStepper_Step(DrvStepperAxis_t axis, uint32_t count, uint32_t pulseUs)
{
    StepperPriv_t *stepper;
    uint32_t i;

    if (!stepper_axis_valid(axis) || count == 0u ||
        pulseUs < STEPPER_MIN_PULSE_US || pulseUs > STEPPER_MAX_PULSE_US ||
        count > (STEPPER_MAX_MOVE_US / (2u * pulseUs))) {
        return -1;
    }
    if (!s_enabled || s_busy) return -2;

    stepper = &s_steppers[axis];
    s_busy = 1u;
    for (i = 0; i < count; i++) {
        /* 死规则：朝 min 限位方向(direction==0)且限位已触发 → 立即停脉冲并把
         * 该轴位置清零，防止继续撞机床/床面。朝反方向(离开限位)放行，保证回零
         * 回退、以及回零后抬起不被卡死。position 改为逐脉冲累加，提前中断时
         * 也能反映真实已走步数。 */
        if (stepper->direction == 0u && stepper_limit_hit(stepper)) {
            stepper->position = 0;
            break;
        }
        HAL_GPIO_WritePin(stepper->stepPort, stepper->stepPin, GPIO_PIN_SET);
        stepper_delay_us(pulseUs);
        HAL_GPIO_WritePin(stepper->stepPort, stepper->stepPin, GPIO_PIN_RESET);
        stepper_delay_us(pulseUs);
        if (stepper->direction) {
            stepper->position++;
        } else {
            stepper->position--;
        }
    }
    s_busy = 0u;
    return 0;
}

static int stepper_move_group(const DrvStepperMoveCommand_t *command)
{
    uint32_t counts[DRV_STEPPER_COUNT];
    uint32_t accumulators[DRV_STEPPER_COUNT] = {0u, 0u, 0u, 0u};
    uint8_t stepped[DRV_STEPPER_COUNT];
    uint32_t maximum = 0u;
    uint32_t tick;
    uint32_t axis;
    uint32_t pulseUs;

    if (!command || !s_enabled || s_busy) return -1;
    pulseUs = command->pulseUs ? command->pulseUs : STEPPER_DEFAULT_US;
    if (pulseUs < STEPPER_MIN_PULSE_US || pulseUs > STEPPER_MAX_PULSE_US) return -1;

    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        int32_t steps = command->steps[axis];
        if (steps == (int32_t)0x80000000u) return -1;
        counts[axis] = (uint32_t)(steps < 0 ? -steps : steps);
        if (counts[axis] > maximum) maximum = counts[axis];
    }
    if (maximum == 0u || maximum > (STEPPER_MAX_MOVE_US / (2u * pulseUs))) return -1;

    for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
        if (counts[axis] > 0u) {
            uint8_t pinDir;
            s_steppers[axis].direction = command->steps[axis] > 0 ? 1u : 0u;
            pinDir = (uint8_t)(s_steppers[axis].direction ^ s_steppers[axis].dirInvert);
            HAL_GPIO_WritePin(s_steppers[axis].dirPort, s_steppers[axis].dirPin,
                              pinDir ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    s_busy = 1u;
    for (tick = 0u; tick < maximum; tick++) {
        /* 死规则：任一轴朝 min 限位方向(direction==0)且已触发 → 停该轴脉冲 + 位置清零 */
        for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
            if (counts[axis] > 0u && s_steppers[axis].direction == 0u &&
                stepper_limit_hit(&s_steppers[axis])) {
                counts[axis] = 0u;
                s_steppers[axis].position = 0;
            }
        }
        memset(stepped, 0, sizeof(stepped));
        for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
            accumulators[axis] += counts[axis];
            if (counts[axis] > 0u && accumulators[axis] >= maximum) {
                accumulators[axis] -= maximum;
                HAL_GPIO_WritePin(s_steppers[axis].stepPort,
                                  s_steppers[axis].stepPin, GPIO_PIN_SET);
                stepped[axis] = 1u;
            }
        }
        stepper_delay_us(pulseUs);
        for (axis = 0u; axis < DRV_STEPPER_COUNT; axis++) {
            if (stepped[axis]) {
                HAL_GPIO_WritePin(s_steppers[axis].stepPort,
                                  s_steppers[axis].stepPin, GPIO_PIN_RESET);
                s_steppers[axis].position += s_steppers[axis].direction ? 1 : -1;
            }
        }
        stepper_delay_us(pulseUs);
    }
    s_busy = 0u;
    return 0;
}

int32_t DrvStepper_GetPosition(DrvStepperAxis_t axis)
{
    return stepper_axis_valid(axis) ? s_steppers[axis].position : 0;
}

int DrvStepper_SetPosition(DrvStepperAxis_t axis, int32_t position)
{
    if (!stepper_axis_valid(axis) || s_busy) return -1;
    s_steppers[axis].position = position;
    return 0;
}

int DrvStepper_GetLimit(DrvStepperAxis_t axis)
{
    StepperPriv_t *stepper;
    int raw;

    if (!stepper_axis_valid(axis)) return -1;
    stepper_gpio_init();
    stepper = &s_steppers[axis];
    if (!stepper->limitPort || stepper->limitPin == 0u) return -2;
    raw = HAL_GPIO_ReadPin(stepper->limitPort, stepper->limitPin) == GPIO_PIN_SET;
    return STEPPER_LIMIT_ACTIVE_HIGH ? raw : !raw;
}

static int stepper_drv_init(void *priv)
{
    StepperPriv_t *stepper = (StepperPriv_t *)priv;

    stepper_gpio_init();
    HAL_GPIO_WritePin(stepper->stepPort, stepper->stepPin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(stepper->dirPort, stepper->dirPin, GPIO_PIN_RESET);
    stepper->direction = 0u;
    stepper->position = 0;
    DBG("[Stepper] %s ready: STEP=%s DIR=%s LIMIT=%s EN=PC3(active-low)\n",
        stepper->axisName, stepper->stepPinName, stepper->dirPinName,
        stepper->limitPinName ? stepper->limitPinName : "-");
    return 0;
}

static int stepper_drv_ioctl(void *priv, uint32_t cmd, void *arg)
{
    StepperPriv_t *stepper = (StepperPriv_t *)priv;

    if (!arg) return -1;
    switch (cmd) {
        case DRV_STEPPER_IOCTL_ENABLE:
            return DrvStepper_EnableAll(*(int *)arg);
        case DRV_STEPPER_IOCTL_DIRECTION:
            return DrvStepper_SetDirection(stepper->axis, *(int *)arg);
        case DRV_STEPPER_IOCTL_STEP:
            return DrvStepper_Step(stepper->axis, *(uint32_t *)arg, stepper->pulseUs);
        case DRV_STEPPER_IOCTL_POSITION:
            return DrvStepper_SetPosition(stepper->axis, *(int32_t *)arg);
        default:
            return -1;
    }
}

static int stepper_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    StepperPriv_t *stepper = (StepperPriv_t *)priv;
    DrvStepperStatus_t status;
    int limit;

    if (!stepper || !buf || len < sizeof(status)) return -1;
    limit = DrvStepper_GetLimit(stepper->axis);
    status.position = stepper->position;
    status.pulseUs = stepper->pulseUs;
    status.direction = stepper->direction;
    status.enabled = s_enabled;
    status.busy = s_busy;
    status.limitTriggered = limit > 0 ? 1u : 0u;
    memcpy(buf, &status, sizeof(status));
    return (int)sizeof(status);
}

static int stepper_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    StepperPriv_t *stepper = (StepperPriv_t *)priv;
    DrvStepperCommand_t command;
    uint32_t count;

    if (!stepper || !buf || len != sizeof(command)) return -1;
    memcpy(&command, buf, sizeof(command));
    if (command.steps == 0 || command.steps == (int32_t)0x80000000u) return -1;
    count = (uint32_t)(command.steps < 0 ? -command.steps : command.steps);
    if (DrvStepper_SetDirection(stepper->axis, command.steps > 0) != 0) return -1;
    if (DrvStepper_Step(stepper->axis, count,
                        command.pulseUs ? command.pulseUs : stepper->pulseUs) != 0) {
        return -1;
    }
    return (int)sizeof(command);
}

static int stepper_group_init(void *priv)
{
    (void)priv;
    stepper_gpio_init();
    return 0;
}

static int stepper_group_write(void *priv, const uint8_t *buf, uint32_t len)
{
    DrvStepperMoveCommand_t command;
    (void)priv;

    if (!buf || len != sizeof(command)) return -1;
    memcpy(&command, buf, sizeof(command));
    if (stepper_move_group(&command) != 0) return -1;
    return (int)sizeof(command);
}

static int stepper_group_ioctl(void *priv, uint32_t cmd, void *arg)
{
    (void)priv;
    if (cmd == DRV_STEPPER_IOCTL_ENABLE && arg) {
        return DrvStepper_EnableAll(*(int *)arg);
    }
    if (cmd == DRV_STEPPER_IOCTL_STOP) {
        DrvStepper_EnableAll(0);
        return 0;
    }
    return -1;
}

static int get_enable(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", s_enabled);
}

static int set_enable(const char *value, void *userData)
{
    (void)userData;
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) return -1;
    return DrvStepper_EnableAll(value[0] == '1');
}

static int get_direction(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    return snprintf(buf, maxLen, "%u", stepper->direction);
}

static int set_direction(const char *value, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) return -1;
    return DrvStepper_SetDirection(stepper->axis, value[0] == '1');
}

static int get_pulse_us(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)stepper->pulseUs);
}

static int set_pulse_us(const char *value, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    unsigned long pulse;

    if (!value || sscanf(value, "%lu", &pulse) != 1 ||
        pulse < STEPPER_MIN_PULSE_US || pulse > STEPPER_MAX_PULSE_US) return -1;
    stepper->pulseUs = (uint32_t)pulse;
    return 0;
}

/* steps 是只写触发器：写入带符号脉冲数即打脉冲，成功后 position 累加 |steps|。
 * 注册为 get=NULL（见 stepper_params / stepper_limit_params）→ cat steps 由框架
 * 提示 "Write-only parameter (use echo to set)"，累计位移看 position。 */
static int set_steps(const char *value, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    long steps;
    uint32_t count;

    if (!value || sscanf(value, "%ld", &steps) != 1 || steps == 0 ||
        steps > (long)STEPPER_MAX_CMD_STEPS ||
        steps < -(long)STEPPER_MAX_CMD_STEPS) return -1;
    count = (uint32_t)((steps < 0) ? -steps : steps);
    if (DrvStepper_SetDirection(stepper->axis, steps > 0) != 0) return -1;
    return DrvStepper_Step(stepper->axis, count, stepper->pulseUs);
}

static int get_position(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    return snprintf(buf, maxLen, "%ld", (long)stepper->position);
}

static int set_position(const char *value, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    long position;
    if (!value || sscanf(value, "%ld", &position) != 1) return -1;
    return DrvStepper_SetPosition(stepper->axis, (int32_t)position);
}

static int get_step_pin(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    return snprintf(buf, maxLen, "%s", stepper->stepPinName);
}

static int get_dir_pin(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    return snprintf(buf, maxLen, "%s", stepper->dirPinName);
}

static int get_en_pin(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "PC3");
}

static int get_limit(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    int triggered = DrvStepper_GetLimit(stepper->axis);
    if (triggered < 0) return -1;
    return snprintf(buf, maxLen, "%d", triggered);
}

static int get_limit_raw(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    int raw;
    if (!stepper->limitPort || stepper->limitPin == 0u) return -1;
    raw = HAL_GPIO_ReadPin(stepper->limitPort, stepper->limitPin) == GPIO_PIN_SET;
    return snprintf(buf, maxLen, "%d", raw);
}

static int get_limit_active(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%s", STEPPER_LIMIT_ACTIVE_HIGH ? "high" : "low");
}

static int get_limit_pin(char *buf, uint16_t maxLen, void *userData)
{
    StepperPriv_t *stepper = (StepperPriv_t *)userData;
    if (!stepper->limitPinName) return -1;
    return snprintf(buf, maxLen, "%s", stepper->limitPinName);
}

static const FsParamDef_t stepper_params[] = {
    FS_PARAM_DEF("enable",   "shared motor enable (active-low pin)", get_enable,    set_enable),
    FS_PARAM_DEF("dir",      "raw direction level (0/1)",           get_direction, set_direction),
    FS_PARAM_DEF("pulse_us", "STEP high and low time in us",        get_pulse_us,  set_pulse_us),
    FS_PARAM_DEF("steps",    "write signed pulse count",            NULL,          set_steps),
    FS_PARAM_DEF("position", "software pulse position",             get_position,  set_position),
    FS_PARAM_DEF("step_pin", "STEP GPIO",                           get_step_pin,  NULL),
    FS_PARAM_DEF("dir_pin",  "DIR GPIO",                            get_dir_pin,   NULL),
    FS_PARAM_DEF("en_pin",   "shared enable GPIO",                  get_en_pin,    NULL),
    FS_PARAM_END
};

static const FsParamDef_t stepper_limit_params[] = {
    FS_PARAM_DEF("enable",    "shared motor enable (active-low pin)", get_enable,    set_enable),
    FS_PARAM_DEF("dir",       "raw direction level (0/1)",           get_direction, set_direction),
    FS_PARAM_DEF("pulse_us",  "STEP high and low time in us",        get_pulse_us,  set_pulse_us),
    FS_PARAM_DEF("steps",     "write signed pulse count",            NULL,          set_steps),
    FS_PARAM_DEF("position",  "software pulse position",             get_position,  set_position),
    FS_PARAM_DEF("limit",     "minimum endstop triggered (0/1)",      get_limit,     NULL),
    FS_PARAM_DEF("limit_raw", "raw endstop GPIO level (0/1)",        get_limit_raw, NULL),
    FS_PARAM_DEF("limit_active", "configured active level",          get_limit_active, NULL),
    FS_PARAM_DEF("step_pin",  "STEP GPIO",                           get_step_pin,  NULL),
    FS_PARAM_DEF("dir_pin",   "DIR GPIO",                            get_dir_pin,   NULL),
    FS_PARAM_DEF("en_pin",    "shared enable GPIO",                  get_en_pin,    NULL),
    FS_PARAM_DEF("limit_pin", "minimum endstop GPIO",                get_limit_pin, NULL),
    FS_PARAM_END
};

#define STEPPER_DEVICE(n, d, p, prm) { \
    .name = n, .desc = d, .bus = DRV_BUS_GPIO, .init = stepper_drv_init, \
    .deinit = NULL, .open = NULL, .close = NULL, \
    .read = stepper_drv_read, .write = stepper_drv_write, \
    .ioctl = stepper_drv_ioctl, .params = prm, .privData = p \
}

static DrvDevice_t s_stepperDevices[DRV_STEPPER_COUNT] = {
    STEPPER_DEVICE("stepper_x", "X stepper driver", &s_steppers[DRV_STEPPER_X], stepper_limit_params),
    STEPPER_DEVICE("stepper_y", "Y stepper driver", &s_steppers[DRV_STEPPER_Y], stepper_limit_params),
    STEPPER_DEVICE("stepper_z", "Z stepper driver", &s_steppers[DRV_STEPPER_Z], stepper_limit_params),
    STEPPER_DEVICE("stepper_e", "E stepper driver", &s_steppers[DRV_STEPPER_E], stepper_params),
};

static DrvDevice_t s_stepperGroupDevice = {
    .name = "stepper_group",
    .desc = "coordinated X/Y/Z/E stepper driver",
    .bus = DRV_BUS_GPIO,
    .init = stepper_group_init,
    .deinit = NULL,
    .open = NULL,
    .close = NULL,
    .read = NULL,
    .write = stepper_group_write,
    .ioctl = stepper_group_ioctl,
    .params = NULL,
    .privData = NULL
};

/* ============================================================
 * 限位 GPIO 变化监控 (调试用)
 * ------------------------------------------------------------
 * 通过事件总线发布 GPIO 变化, 并在订阅者里打印到串口,
 * 用于排查“短接限位但 cat limit 不变”等接线/极性问题。
 * 轮询运行在 Banux 组件 process 钩子 (主循环线程上下文, 可安全 DBG 打印)。
 * ============================================================ */
static uint8_t s_lastLimitRaw[3];   /* 索引 0=X,1=Y,2=Z；0xFF=无效轴 */

static int DrvStepper_LimitMonitorInit(void)
{
    uint8_t i;

    stepper_gpio_init();   /* 确保限位引脚已配置为输入 */
    for (i = 0u; i < 3u; i++) {
        StepperPriv_t *st = &s_steppers[i];
        if (st->limitPort && st->limitPin) {
            s_lastLimitRaw[i] = (HAL_GPIO_ReadPin(st->limitPort, st->limitPin)
                                 == GPIO_PIN_SET) ? 1u : 0u;
        } else {
            s_lastLimitRaw[i] = 0xFFu;
        }
    }
    DBG("[Stepper] limit monitor started (X=PA5 Y=PA6 Z=PA7)\n");
    return 0;
}

static void DrvStepper_PollLimit(void)
{
    uint8_t i;

    /* 使能保持（需求：电机使能且无操作时不得被外力移动）：
     * 只要处于使能态就每拍把 EN(PC3, active-low) 重新拉低，保证驱动器绕组持续
     * 通电、维持保持扭矩锁住转子；即便其它代码路径意外改写 EN，也会在下一拍被
     * 纠正。未使能(s_enabled==0)时不触碰 EN，保持高电平→驱动器释放→可自由推动。 */
    if (s_enabled && s_gpioInitialized) {
        HAL_GPIO_WritePin(STEPPER_EN_PORT, STEPPER_EN_PIN, GPIO_PIN_RESET);
    }

    for (i = 0u; i < 3u; i++) {
        StepperPriv_t *st = &s_steppers[i];
        uint8_t raw;
        BG_EventGpioData_t d;
        uint16_t mask;
        uint8_t pin_no;

        if (!st->limitPort || st->limitPin == 0u) continue;

        raw = (HAL_GPIO_ReadPin(st->limitPort, st->limitPin) == GPIO_PIN_SET) ? 1u : 0u;
        if (raw == s_lastLimitRaw[i]) continue;   /* 无变化, 跳过 */
        s_lastLimitRaw[i] = raw;

        /* 死规则(空闲侧)：限位状态跳变为“触发”时把该轴位置清零——即便电机空闲
         * 被外力推到限位也归零。仅在跳变沿执行, 避免停在限位上时对离开方向的
         * 小位移反复清零(运动中的急停清零由 DrvStepper_Step 负责)。 */
        if (STEPPER_LIMIT_ACTIVE_HIGH ? raw : !raw) {
            st->position = 0;
        }

        /* GPIO_PIN_x 位掩码 -> 引脚号 0..15 */
        mask = st->limitPin;
        pin_no = 0u;
        while (mask > 1u) { mask >>= 1; pin_no++; }

        d.port_letter = (st->limitPort == GPIOA) ? 'A'
                      : (st->limitPort == GPIOB) ? 'B'
                      : (st->limitPort == GPIOC) ? 'C' : '?';
        d.pin       = pin_no;
        d.raw_level = raw;
        d.idx       = (uint8_t)st->axis;
        {
            const char *n = st->limitPinName ? st->limitPinName : "?";
            uint8_t k = 0u;
            while (k < (uint8_t)(sizeof(d.name) - 1u) && n[k] != '\0') {
                d.name[k] = n[k]; k++;
            }
            d.name[k] = '\0';
        }
        BG_EVT_PUB_DATA(EVT_GPIO_CHANGED, &d, sizeof(d));
    }
}

/* 订阅者: 收到 GPIO 变化即打印到串口 */
static void on_gpio_changed(BG_EventTopic_t topic, const void *data, uint8_t size)
{
    const BG_EventGpioData_t *d = (const BG_EventGpioData_t *)data;
    char axis_ch;
    (void)topic;
    (void)size;
    if (!d) return;
    axis_ch = (d->idx < 3u) ? "XYZ"[d->idx] : '?';
    DBG("[GPIO] %s raw=%d limit=%d axis=%c\n",
        d->name,
        (int)d->raw_level,
        STEPPER_LIMIT_ACTIVE_HIGH ? (int)d->raw_level : (int)(!d->raw_level),
        axis_ch);
}
BG_EVT_SUB(EVT_GPIO_CHANGED, on_gpio_changed);

/* 注册为 Banux 组件: 主循环 process 钩子调用 DrvStepper_PollLimit (线程上下文) */
BANUX_COMPONENT_DEFINE_EX(
    g_banux_component_stepper_limit_mon,
    "stepper_limit_mon", "1.0.0",
    BANUX_COMPONENT_APPLICATION, 1,
    "X/Y/Z 限位 GPIO 变化监控 (发布 EVT_GPIO_CHANGED)",
    DrvStepper_LimitMonitorInit, DrvStepper_PollLimit);

int DrvStepper_Register(void)
{
    uint32_t i;

    for (i = 0; i < DRV_STEPPER_COUNT; i++) {
        int ret = DrvDevice_Register(&s_stepperDevices[i]);
        if (ret != 0) {
            DrvStepper_EnableAll(0);
            DBG("[Stepper] register %s failed: %d\n", s_steppers[i].axisName, ret);
            return -1;
        }
    }
    if (DrvDevice_Register(&s_stepperGroupDevice) != 0) {
        DrvStepper_EnableAll(0);
        DBG("[Stepper] register coordinated group failed\n");
        return -1;
    }
    return 0;
}
