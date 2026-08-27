#include <stdio.h>
#include <string.h>
#include "drv_stepper.h"
#include "drv_device.h"
#include "debug.h"
#include "banux_config.h"
#include "stm32f1xx_hal.h"

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
} StepperPriv_t;

static StepperPriv_t s_steppers[DRV_STEPPER_COUNT] = {
    { DRV_STEPPER_X, "X", GPIOB, GPIO_PIN_9, GPIOC, GPIO_PIN_2,
      GPIOC, GPIO_PIN_0, "PB9", "PC2", "PC0", STEPPER_DEFAULT_US, 0, 0 },
    { DRV_STEPPER_Y, "Y", GPIOB, GPIO_PIN_7, GPIOB, GPIO_PIN_8,
      GPIOC, GPIO_PIN_1, "PB7", "PB8", "PC1", STEPPER_DEFAULT_US, 0, 0 },
    { DRV_STEPPER_Z, "Z", GPIOB, GPIO_PIN_5, GPIOB, GPIO_PIN_6,
      GPIOA, GPIO_PIN_15, "PB5", "PB6", "PA15", STEPPER_DEFAULT_US, 0, 0 },
    { DRV_STEPPER_E, "E", GPIOB, GPIO_PIN_3, GPIOB, GPIO_PIN_4,
      NULL, 0, "PB3", "PB4", NULL, STEPPER_DEFAULT_US, 0, 0 },
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

    /* The board supplies 10K pull-ups and 100 nF filters on all endstops. */
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    init.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOC, &init);
    init.Pin = GPIO_PIN_15;
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

    if (!stepper_axis_valid(axis)) return -1;
    stepper_gpio_init();
    stepper = &s_steppers[axis];
    stepper->direction = direction ? 1u : 0u;
    HAL_GPIO_WritePin(stepper->dirPort, stepper->dirPin,
                      stepper->direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return 0;
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
        HAL_GPIO_WritePin(stepper->stepPort, stepper->stepPin, GPIO_PIN_SET);
        stepper_delay_us(pulseUs);
        HAL_GPIO_WritePin(stepper->stepPort, stepper->stepPin, GPIO_PIN_RESET);
        stepper_delay_us(pulseUs);
    }
    if (stepper->direction) {
        stepper->position += (int32_t)count;
    } else {
        stepper->position -= (int32_t)count;
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
            s_steppers[axis].direction = command->steps[axis] > 0 ? 1u : 0u;
            HAL_GPIO_WritePin(s_steppers[axis].dirPort, s_steppers[axis].dirPin,
                              s_steppers[axis].direction ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }

    s_busy = 1u;
    for (tick = 0u; tick < maximum; tick++) {
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
    DBG("[Stepper] %s ready: STEP=%s DIR=%s EN=PC3(active-low)\n",
        stepper->axisName, stepper->stepPinName, stepper->dirPinName);
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

static int get_steps(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "0");
}

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
    FS_PARAM_DEF("steps",    "write signed pulse count",            get_steps,     set_steps),
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
    FS_PARAM_DEF("steps",     "write signed pulse count",            get_steps,     set_steps),
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
