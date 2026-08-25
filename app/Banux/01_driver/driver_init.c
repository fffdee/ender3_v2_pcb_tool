/** @file driver_init.c @brief Ender-3 V2 platform driver registration. */
#include "driver_init.h"
#include "drv_eeprom.h"
#include "drv_sdio.h"
#include "drv_stepper.h"
#include "drv_timer1ms.h"
#include "drv_uart.h"
#include "debug.h"

int BanuxDriver_RegisterAll(void)
{
    int ret;

    ret = DrvTimer1ms_Register();
    if (ret != 0) {
        DBG("[DriverInit] timer1ms register failed (%d)\n", ret);
        return -1;
    }

    ret = DrvStepper_Register();
    if (ret != 0) {
        DBG("[DriverInit] stepper register failed (%d)\n", ret);
        return -2;
    }

    ret = DrvEeprom_Register();
    if (ret != 0) {
        DBG("[DriverInit] WARNING: EEPROM register failed (%d)\n", ret);
    }

    ret = DrvSdio_Register();
    if (ret != 0) {
        DBG("[DriverInit] WARNING: SDIO register failed (%d), "
            "SD card may not be present\n", ret);
    }

    ret = DrvUart_Register();
    if (ret != 0) {
        DBG("[DriverInit] UART register failed (%d)\n", ret);
        return -3;
    }

    DBG("[DriverInit] Platform drivers registered\n");
    return 0;
}
