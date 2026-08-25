/**
 * @file  app_upgrade.h
 * @brief Firmware upgrade engine — Bootloader + dual-partition scheme.
 *
 * Architecture
 * ────────────
 * A separate bootloader runs at 0x000000 and jumps to the active partition.
 * The application firmware runs at 0x040000 (Partition A) or via address
 * remap (Partition B at 0x240000).
 *
 * Upgrade channels:
 *   - USB CDC: handled in cdc_upgrade.c, packets feed into this engine.
 *   - BLE OTA: packets arrive via GATT AB01, feed into this engine.
 *
 * Both channels share the same protocol and state machine.
 * Upgrade ALWAYS writes to Partition B (0x240000).
 * If currently running Partition B, the engine refuses and returns
 * UPG_ERR_WRONG_PART — the app must reboot to Partition A first.
 *
 * Upgrade flow:
 *   1. SYNC  → ACK with protocol version
 *   2. QUERY → ACK with device info (partition layout, active part, etc.)
 *   3. START → validate size, erase Partition B, set state=WRITING
 *   4. DATA  → write chunks to Partition B
 *   5. FINISH→ verify magic, set active_part=1, reboot
 */
#ifndef __APP_UPGRADE_H__
#define __APP_UPGRADE_H__

#include <stdint.h>
#include "dual_partition.h"

/* ── Channel IDs ─────────────────────────────────────────────────────────── */
#define UPG_CH_CDC  0
#define UPG_CH_BLE  1

/* ── Channel interface ───────────────────────────────────────────────────── */
typedef struct {
    uint16_t (*rx_read)     (uint8_t *buf, uint16_t maxLen);
    void     (*tx_write)    (const uint8_t *buf, uint16_t len);
    int      (*rx_available)(void);
    uint8_t  id;
} UpgradeChannel_t;

/* ── Public API ──────────────────────────────────────────────────────────── */

/**
 * @brief  Initialise the upgrade engine. Call once at startup.
 */
void App_Upgrade_Init(void);

/**
 * @brief  Process one iteration of the upgrade state machine for a channel.
 *         Call from main loop for each active channel.
 */
void App_Upgrade_ProcessChannel(const UpgradeChannel_t *ch);

/**
 * @brief  Returns 1 if any channel is actively writing firmware.
 *         Used to pause audio/other tasks during upgrade.
 */
int App_Upgrade_IsActive(void);

/**
 * @brief  Returns 1 if the upgrade has finished successfully (STATE_FINISH).
 *         After this, the caller should trigger a system reset.
 */
int App_Upgrade_IsFinished(void);

/**
 * @brief  Inject a pre-read raw packet into the upgrade engine.
 *         Used by cdc_upgrade.c when bytes have already been consumed
 *         from the OTG CDC FIFO (e.g. after SOF detection via GetChar).
 * @param  ch_id   UPG_CH_CDC or UPG_CH_BLE
 * @param  buf     Packet bytes (first byte should be UPG_SOF)
 * @param  len     Number of bytes in buf
 * @param  tx_fn   Function to send response bytes back to the host
 */
void App_Upgrade_InjectRaw(uint8_t ch_id, const uint8_t *buf, uint16_t len,
                           void (*tx_fn)(const uint8_t *data, uint16_t len));

/* ── BLE OTA convenience API (backward-compatible names) ─────────────────── */

/**
 * @brief  Initialise BLE OTA. Registers the BLE send callback.
 */
void App_OTA_Init(void (*send_fn)(const uint8_t *data, uint16_t len));

/**
 * @brief  Feed raw BLE bytes into the OTA packet parser.
 */
void App_OTA_OnData(const uint8_t *data, uint16_t len);

/**
 * @brief  Drive deferred BLE OTA work. Call once per main loop tick.
 */
void App_OTA_Process(void);

/* ── Boot confirmation (delegates to boot_decision.c) ────────────────────── */

/**
 * @brief  Confirm boot success. Resets boot_fail_cnt in partition flags.
 */
void App_ConfirmBootSuccess(void);

#endif /* __APP_UPGRADE_H__ */
