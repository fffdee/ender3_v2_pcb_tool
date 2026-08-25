/** @file drv_init.c @brief Portable Banux driver framework initialization. */
#include "drv_init.h"
#include "vfs.h"
#include "drv_fs.h"
#include "drv_device.h"
#include "debug.h"
#include "banux_component.h"

BANUX_COMPONENT_DEFINE(g_banux_component_driver_framework,
                       "driver_framework", "2.0.0",
                       BANUX_COMPONENT_SYSTEM, DRV_DEVICE_EN,
                       "portable device model and driver filesystem");

int DrvFramework_Init(void)
{
    int ret;

    ret = Vfs_Init();
    if (ret != VFS_OK) {
        DBG("[DrvInit] VFS init failed!\n");
        BanuxComponent_SetState("driver_framework", BANUX_COMPONENT_FAILED);
        return -1;
    }

    ret = DrvFs_Init();
    if (ret != FS_OK) {
        DBG("[DrvInit] DrvFs init failed!\n");
        BanuxComponent_SetState("driver_framework", BANUX_COMPONENT_FAILED);
        return -2;
    }

    ret = DrvDevice_Init();
    if (ret != 0) {
        DBG("[DrvInit] DrvDevice init failed!\n");
        BanuxComponent_SetState("driver_framework", BANUX_COMPONENT_FAILED);
        return -3;
    }

    BanuxComponent_SetState("driver_framework", BANUX_COMPONENT_READY);
    return 0;
}

int DrvFramework_FullInit(void)
{
    return DrvFramework_Init();
}
