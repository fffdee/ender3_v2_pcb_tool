/**
 * @file    drv_hc245.c
 * @brief   74HC245 并行 IO 扩展驱动实现（仿 drv_stepper 的完整度）。
 *
 * 接线（原理图 U10 = 74HC245，Ender-3 V2 / Creality 4.2.2）：
 *   MCU PA0 -> pin7  (A5) -> 输出 B5
 *   MCU PA1 -> pin8  (A6) -> 输出 B6
 *   MCU PA2 -> pin9  (A7) -> 输出 B7
 *   MCU PA3 -> pin6  (A4) -> 输出 B4   [待核对：原理图 pin6 似 no-connect]
 *   pin2..pin5 (A0..A3) 未接；DIR(pin1) 经 R63 10K 上拉 +5V；OE(pin19) 低有效。
 * 散热风扇默认接在 B4（HC245_FAN_BIT），开机由 hc245_drv_init 置位打开。
 * 若实测风扇在 B5/B6/B7，改 HC245_FAN_BIT 即可；若 B4 的 MCU 线不是 PA3，
 * 改 HC245_B4_PORT/HC245_B4_PIN。
 */
#include <stdio.h>
#include <string.h>
#include "drv_hc245.h"
#include "drv_device.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

/* B4 的 MCU A 侧线。原理图 pin6(A4) 似 no-connect，此为占位待核对。 */
#define HC245_B4_PORT       GPIOA
#define HC245_B4_PIN        GPIO_PIN_3

/* 散热风扇输出位（用户指定 B4）。 */
#define HC245_FAN_BIT       4u

typedef struct {
    uint8_t bit;
    GPIO_TypeDef *port;      /* NULL = 该位 no-connect */
    uint16_t pin;
    const char *pinName;
} Hc245Bit_t;

static const Hc245Bit_t s_bits[DRV_HC245_BITS] = {
    { 0u, NULL, 0u, NULL },
    { 1u, NULL, 0u, NULL },
    { 2u, NULL, 0u, NULL },
    { 3u, NULL, 0u, NULL },
    { 4u, HC245_B4_PORT, HC245_B4_PIN, "PA3" },  /* 待核对 */
    { 5u, GPIOA, GPIO_PIN_0, "PA0" },
    { 6u, GPIOA, GPIO_PIN_1, "PA1" },
    { 7u, GPIOA, GPIO_PIN_2, "PA2" },
};

static uint32_t s_value;          /* 8 位输出影子寄存器 */
static uint32_t s_wiredMask;      /* 已接 MCU 线的位掩码 */
static uint8_t s_gpioInitialized;

static uint32_t hc245_build_wired_mask(void)
{
    uint32_t mask = 0u;
    uint32_t i;
    for (i = 0u; i < DRV_HC245_BITS; i++) {
        if (s_bits[i].port != NULL) mask |= (1u << i);
    }
    return mask;
}

static void hc245_gpio_init(void)
{
    GPIO_InitTypeDef init;
    uint32_t i;

    if (s_gpioInitialized) return;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    s_wiredMask = hc245_build_wired_mask();

    /* 先拉低所有已接线，再配置为推挽输出，避免上电毛刺误触发负载。 */
    for (i = 0u; i < DRV_HC245_BITS; i++) {
        if (s_bits[i].port != NULL) {
            HAL_GPIO_WritePin(s_bits[i].port, s_bits[i].pin, GPIO_PIN_RESET);
        }
    }
    init.Mode = GPIO_MODE_OUTPUT_PP;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_LOW;
    for (i = 0u; i < DRV_HC245_BITS; i++) {
        if (s_bits[i].port != NULL) {
            init.Pin = s_bits[i].pin;
            HAL_GPIO_Init(s_bits[i].port, &init);
        }
    }

    s_value = 0u;
    s_gpioInitialized = 1u;
}

int DrvHc245_SetValue(uint32_t value, uint32_t mask)
{
    uint32_t i;

    hc245_gpio_init();
    for (i = 0u; i < DRV_HC245_BITS; i++) {
        uint32_t bitMask = (1u << i);
        if ((mask & bitMask) == 0u) continue;
        if (value & bitMask) {
            s_value |= bitMask;
        } else {
            s_value &= ~bitMask;
        }
        if (s_bits[i].port != NULL) {
            HAL_GPIO_WritePin(s_bits[i].port, s_bits[i].pin,
                              (value & bitMask) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        }
    }
    return 0;
}

uint32_t DrvHc245_GetValue(void)
{
    return s_value;
}

int DrvHc245_SetBit(uint32_t bit, int on)
{
    if (bit >= DRV_HC245_BITS) return -1;
    return DrvHc245_SetValue(on ? (1u << bit) : 0u, (1u << bit));
}

int DrvHc245_GetBit(uint32_t bit)
{
    if (bit >= DRV_HC245_BITS) return -1;
    return (s_value & (1u << bit)) ? 1 : 0;
}

int DrvHc245_Fan(int on)
{
    return DrvHc245_SetBit(HC245_FAN_BIT, on);
}

static int hc245_drv_init(void *priv)
{
    (void)priv;

    hc245_gpio_init();
    /* 需求：默认开机打开散热风扇（B4）。 */
    DrvHc245_Fan(1);
    DBG("[HC245] ready: wired mask=0x%02X fan=bit%u(on) B5=PA0 B6=PA1 B7=PA2\n",
        (unsigned)s_wiredMask, (unsigned)HC245_FAN_BIT);
    return 0;
}

static int hc245_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    DrvHc245Status_t status;
    (void)priv;

    if (!buf || len < sizeof(status)) return -1;
    hc245_gpio_init();
    status.value = s_value;
    status.wiredMask = s_wiredMask;
    status.fanBit = HC245_FAN_BIT;
    status.fanOn = (s_value & (1u << HC245_FAN_BIT)) ? 1u : 0u;
    memcpy(buf, &status, sizeof(status));
    return (int)sizeof(status);
}

static int hc245_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    DrvHc245Command_t command;
    (void)priv;

    if (!buf || len != sizeof(command)) return -1;
    memcpy(&command, buf, sizeof(command));
    if (DrvHc245_SetValue(command.value, command.mask) != 0) return -1;
    return (int)sizeof(command);
}

static int hc245_drv_ioctl(void *priv, uint32_t cmd, void *arg)
{
    (void)priv;
    if (!arg) return -1;
    switch (cmd) {
        case DRV_HC245_IOCTL_SET_VALUE: {
            const DrvHc245Command_t *c = (const DrvHc245Command_t *)arg;
            return DrvHc245_SetValue(c->value, c->mask);
        }
        case DRV_HC245_IOCTL_GET_VALUE:
            *(uint32_t *)arg = DrvHc245_GetValue();
            return 0;
        case DRV_HC245_IOCTL_SET_BIT: {
            const DrvHc245BitCmd_t *b = (const DrvHc245BitCmd_t *)arg;
            return DrvHc245_SetBit(b->bit, (int)b->on);
        }
        case DRV_HC245_IOCTL_CLEAR_BIT:
            return DrvHc245_SetBit(*(uint32_t *)arg, 0);
        case DRV_HC245_IOCTL_FAN:
            return DrvHc245_Fan(*(int *)arg);
        default:
            return -1;
    }
}

/* ---------------- VFS 参数 ---------------- */

static int get_value(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", (unsigned)s_value);
}

static int set_value(const char *value, void *userData)
{
    unsigned long v;
    (void)userData;
    if (!value || sscanf(value, "%lu", &v) != 1) return -1;
    return DrvHc245_SetValue((uint32_t)v, s_wiredMask);
}

static int get_fan(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u",
                    (unsigned)((s_value & (1u << HC245_FAN_BIT)) ? 1u : 0u));
}

static int set_fan(const char *value, void *userData)
{
    (void)userData;
    if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) return -1;
    return DrvHc245_Fan(value[0] == '1');
}

static int get_wired_mask(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", (unsigned)s_wiredMask);
}

static int get_fan_bit(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", (unsigned)HC245_FAN_BIT);
}

static int get_pins(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "b4=%s b5=%s b6=%s b7=%s",
                    s_bits[4].pinName ? s_bits[4].pinName : "-",
                    s_bits[5].pinName ? s_bits[5].pinName : "-",
                    s_bits[6].pinName ? s_bits[6].pinName : "-",
                    s_bits[7].pinName ? s_bits[7].pinName : "-");
}

/* 为每个可控位生成 get_bN / set_bN。 */
#define HC245_BIT_PARAM(n)                                                  \
    static int get_b##n(char *buf, uint16_t maxLen, void *userData)         \
    {                                                                       \
        (void)userData;                                                     \
        return snprintf(buf, maxLen, "%d", DrvHc245_GetBit(n##u));          \
    }                                                                       \
    static int set_b##n(const char *value, void *userData)                  \
    {                                                                       \
        (void)userData;                                                     \
        if (!value || (strcmp(value, "0") != 0 && strcmp(value, "1") != 0)) \
            return -1;                                                      \
        return DrvHc245_SetBit(n##u, value[0] == '1');                      \
    }

HC245_BIT_PARAM(4)
HC245_BIT_PARAM(5)
HC245_BIT_PARAM(6)
HC245_BIT_PARAM(7)

static const FsParamDef_t hc245_params[] = {
    FS_PARAM_DEF("value",      "8-bit output shadow (B0..B7)",   get_value,      set_value),
    FS_PARAM_DEF("fan",        "cooling fan output (0/1)",       get_fan,        set_fan),
    FS_PARAM_DEF("b4",         "output bit4 (fan, verify pin)",  get_b4,         set_b4),
    FS_PARAM_DEF("b5",         "output bit5 (PA0)",              get_b5,         set_b5),
    FS_PARAM_DEF("b6",         "output bit6 (PA1)",              get_b6,         set_b6),
    FS_PARAM_DEF("b7",         "output bit7 (PA2)",              get_b7,         set_b7),
    FS_PARAM_DEF("wired_mask", "bits with MCU A-side line",      get_wired_mask, NULL),
    FS_PARAM_DEF("fan_bit",    "configured fan output bit",      get_fan_bit,    NULL),
    FS_PARAM_DEF("pins",       "B4..B7 MCU GPIO map",            get_pins,       NULL),
    FS_PARAM_END
};

static DrvDevice_t s_hc245Device = {
    .name = "hc245",
    .desc = "74HC245 parallel IO expander (U10)",
    .bus = DRV_BUS_GPIO,
    .init = hc245_drv_init,
    .deinit = NULL,
    .open = NULL,
    .close = NULL,
    .read = hc245_drv_read,
    .write = hc245_drv_write,
    .ioctl = hc245_drv_ioctl,
    .params = hc245_params,
    .privData = NULL
};

int DrvHc245_Register(void)
{
    if (DrvDevice_Register(&s_hc245Device) != 0) {
        DBG("[HC245] register failed\n");
        return -1;
    }
    return 0;
}
