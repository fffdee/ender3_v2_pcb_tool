/**
 * @file  fw_upgrade.c
 * @brief BanUX firmware upgrade component facade.
 */

#include "fw_upgrade.h"
#include "app_upgrade.h"
#include "cdc_upgrade.h"
#include "dual_partition.h"
#include "spi_flash.h"
#include "reset.h"

void FwUpgrade_BootInit(void)
{
    DualPart_Init();
    Boot_CheckAndJump();
}

void FwUpgrade_ConfirmBootSuccess(void)
{
    Boot_ConfirmSuccess();
}

void FwUpgrade_Init(void)
{
    App_Upgrade_Init();
    CDC_Upgrade_Init();
}

void FwUpgrade_EnterCdcMode(void)
{
    CDC_Upgrade_EnterMode();
}

int FwUpgrade_InCdcMode(void)
{
    return CDC_Upgrade_InMode();
}

int FwUpgrade_CheckCdcEnter(void)
{
    return CDC_Upgrade_CheckEnter();
}

void FwUpgrade_ProcessCdc(void)
{
    CDC_Upgrade_Process();
}

int FwUpgrade_IsActive(void)
{
    return CDC_Upgrade_IsActive();
}

int FwUpgrade_GetInfo(FwUpgradeInfo_t *info)
{
    PartFlag_t flags;
    const DualPart_Layout_t *layout;
    int flags_valid;

    if (!info) {
        return 0;
    }

    layout = DualPart_GetLayout();
    flags_valid = PartFlag_Read(&flags);

    info->part_a_base = PART_A_BASE;
    info->part_a_size = layout->part_a_usable;
    info->part_b_base = PART_B_BASE;
    info->part_b_size = layout->part_b_usable;
    info->flags_addr = layout->part_flag_addr;
    info->flags_valid = (uint8_t)(flags_valid ? 1u : 0u);
    info->active_part = flags_valid ? flags.active_part : 0u;
    info->boot_fail_cnt = flags_valid ? flags.boot_fail_cnt : 0u;
    info->boot_fail_max = BOOT_FAIL_MAX;
    info->running_part_b = (uint8_t)(Boot_IsRunningPart2() ? 1u : 0u);

    return flags_valid;
}

void FwUpgrade_RebootToBootloader(void)
{
    uint32_t magic = BURN_FLAG_MAGIC;

    SpiFlashIOCtrl(IOCTL_FLASH_UNPROTECT, "\x35\xBA\x69", 3);
    SpiFlashErase(SECTOR_ERASE, BURN_FLAG_SECTOR, 1);
    SpiFlashWrite(BURN_FLAG_ADDR, (uint8_t *)&magic, sizeof(magic), 100);
    Reset_McuSystem();
}

uint32_t FwUpgrade_GetBootloaderFlag(void)
{
    return *(volatile const uint32_t *)BURN_FLAG_ADDR;
}

int FwUpgrade_IsBootloaderFlagSet(void)
{
    return (FwUpgrade_GetBootloaderFlag() == BURN_FLAG_MAGIC) ? 1 : 0;
}
