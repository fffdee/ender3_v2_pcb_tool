#include "internal_flash_fs.h"
#include "banux_config.h"
#include "banux_component.h"
#include "fatfs.h"
#include "fs_sd.h"
#include "debug.h"

BANUX_COMPONENT_DEFINE(g_banux_component_internal_flash_fs,
                       "internal_flash_fs", "1.0.0",
                       BANUX_COMPONENT_SYSTEM,
                       BANUX_INTERNAL_FLASH_FS_EN,
                       "20 KB internal Flash FAT filesystem");

int InternalFlashFs_Init(void)
{
#if BANUX_INTERNAL_FLASH_FS_EN
    FRESULT result;

    if (retFlash != 0u) {
        BanuxComponent_SetState("internal_flash_fs", BANUX_COMPONENT_FAILED);
        DBG("[FlashFs] disk driver is not linked\n");
        return -1;
    }

    result = f_mount(&FlashFatFS, FlashPath, 1u);
    if (result != FR_OK) {
        BanuxComponent_SetState("internal_flash_fs", BANUX_COMPONENT_FAILED);
        DBG("[FlashFs] f_mount failed: result=%d\n", (int)result);
        return -(int)result;
    }
    if (FatFsVfs_Mount("flash", 1u) != 0) {
        BanuxComponent_SetState("internal_flash_fs", BANUX_COMPONENT_FAILED);
        return -2;
    }

    BanuxComponent_SetState("internal_flash_fs", BANUX_COMPONENT_READY);
    DBG("[FlashFs] internal Flash FAT mounted to /flash\n");
    return 0;
#else
    return 0;
#endif
}
