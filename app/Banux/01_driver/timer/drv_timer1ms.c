/** @file drv_timer1ms.c @brief SysTick/HAL 1 ms timer exposed as a driver. */
#include <stdio.h>
#include <string.h>
#include "stm32f1xx_hal.h"
#include "banux_config.h"
#include "drv_timer1ms.h"
#include "drv_device.h"

uint32_t DrvTimer1ms_Now(void)
{
    return HAL_GetTick();
}

int DrvTimer1ms_Expired(uint32_t deadline)
{
    return ((int32_t)(DrvTimer1ms_Now() - deadline) >= 0) ? 1 : 0;
}

#if TIMER1MS_EN
static int timer_read(void *priv, uint8_t *buf, uint32_t len)
{
    uint32_t now;
    (void)priv;
    if (!buf || len < sizeof(now)) return -1;
    now = DrvTimer1ms_Now();
    memcpy(buf, &now, sizeof(now));
    return (int)sizeof(now);
}

static int timer_get_now(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)DrvTimer1ms_Now());
}

static const FsParamDef_t timer_params[] = {
    FS_PARAM_DEF("now_ms", "milliseconds since startup", timer_get_now, NULL),
    FS_PARAM_END
};

static DrvDevice_t timer1ms_device = {
    .name = "timer1ms",
    .desc = "SysTick hardware 1 ms timebase",
    .bus = DRV_BUS_TIMER,
    .init = NULL,
    .deinit = NULL,
    .open = NULL,
    .close = NULL,
    .read = timer_read,
    .write = NULL,
    .ioctl = NULL,
    .params = timer_params,
    .privData = NULL,
};
#endif

int DrvTimer1ms_Register(void)
{
#if TIMER1MS_EN
    return DrvDevice_Register(&timer1ms_device);
#else
    return 0;
#endif
}
