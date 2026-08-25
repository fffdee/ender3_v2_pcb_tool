/**
 * @file  fw_upgrade.h
 * @brief BanUX firmware upgrade component facade.
 *
 * This component owns the application-facing bootloader upgrade flow:
 * boot partition setup, CDC upgrade mode, partition information query, and
 * rebooting into the bootloader stay mode.
 */
#ifndef __FW_UPGRADE_H__
#define __FW_UPGRADE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t part_a_base;
    uint32_t part_a_size;
    uint32_t part_b_base;
    uint32_t part_b_size;
    uint32_t flags_addr;
    uint8_t  flags_valid;
    uint8_t  active_part;
    uint8_t  boot_fail_cnt;
    uint8_t  boot_fail_max;
    uint8_t  running_part_b;
} FwUpgradeInfo_t;

/**
 * @brief Initialize partition layout and detect the running partition.
 *        Call early before the upgrade engine uses partition metadata.
 */
void FwUpgrade_BootInit(void);

/**
 * @brief Confirm current firmware booted successfully.
 */
void FwUpgrade_ConfirmBootSuccess(void);

/**
 * @brief Initialize CDC/BLE firmware upgrade engine.
 */
void FwUpgrade_Init(void);

/**
 * @brief Enter CDC firmware upgrade mode from shell.
 */
void FwUpgrade_EnterCdcMode(void);

/**
 * @brief Check whether CDC upgrade mode is active.
 */
int FwUpgrade_InCdcMode(void);

/**
 * @brief Probe CDC RX for upgrade SOF and enter upgrade mode if found.
 */
int FwUpgrade_CheckCdcEnter(void);

/**
 * @brief Process CDC firmware upgrade mode.
 */
void FwUpgrade_ProcessCdc(void);

/**
 * @brief Return 1 while firmware data is being written.
 */
int FwUpgrade_IsActive(void);

/**
 * @brief Fill current boot partition information.
 * @return 1 if partition flags are valid, 0 otherwise.
 */
int FwUpgrade_GetInfo(FwUpgradeInfo_t *info);

/**
 * @brief Write bootloader stay flag and reboot.
 * @note This function does not return on success.
 */
void FwUpgrade_RebootToBootloader(void);

/**
 * @brief Read current bootloader stay flag raw value.
 */
uint32_t FwUpgrade_GetBootloaderFlag(void);

/**
 * @brief Return 1 if bootloader stay flag is set.
 */
int FwUpgrade_IsBootloaderFlagSet(void);

#ifdef __cplusplus
}
#endif

#endif /* __FW_UPGRADE_H__ */
