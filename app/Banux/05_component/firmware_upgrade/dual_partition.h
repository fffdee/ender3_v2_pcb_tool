/**
 * @file  dual_partition.h
 * @brief Bootloader + dual-partition layout and data structures.
 *
 * Architecture (WITH separate bootloader)
 * ─────────────────────────────────────
 * Bootloader runs from 0x000000 (256KB). On boot it checks partition
 * flags and jumps to the active partition (A or B).
 * The BanBox APP is linked to start at 0x040000 (Partition A).
 * When Partition B is active, hardware address remap maps
 * [0x040000, 0x240000) → [0x240000, 0x440000) transparently.
 *
 * Flash layout (8 MB / 0x800000) — dual-partition:
 *   0x000000 - 0x03FFFF  Bootloader             256 KB
 *   0x040000 - 0x23FFFF  Partition A            2 MB  — equal live partition
 *   0x240000 - 0x43FFFF  Partition B            2 MB  — equal live partition
 *   0x440000 - 0x440FFF  Partition flags         4 KB
 *   0x441000 - 0x7FFFFF  System data / BT config ~3.75 MB
 *
 * Flash layout (2 MB / 0x200000) — single-partition:
 *   0x000000 - 0x03FFFF  Bootloader             256 KB
 *   0x040000 - 0x1FEFFF  Partition A            ~1.75 MB (only partition)
 *   0x1FF000 - 0x1FFFFF  Partition flags         4 KB (last sector)
 *   (Partition B not available — upgrade overwrites Partition A)
 */
#ifndef __DUAL_PARTITION_H__
#define __DUAL_PARTITION_H__

#include "type.h"

/* ── Flash partition layout (aligned with bootloader upgrade.h) ───────────── */
#define BOOTLOADER_SIZE     0x00040000UL  /* 256 KB                         */

/* Internal ROM capacity (no external NOR Flash) */
#define INTERNAL_ROM_CAPACITY 0x00200000UL /* 2 MB internal flash            */

#define PART_A_BASE         0x00040000UL  /* Partition A base               */
#define PART_A_SIZE         0x00200000UL  /* Partition A: 2 MB (max)        */
#define PART_B_BASE         0x00240000UL  /* Partition B base               */
#define PART_B_SIZE         0x00200000UL  /* Partition B: 2 MB              */

/* Default PartFlag address for 8 MB flash; 2 MB flash uses last sector */
#define PART_FLAG_ADDR_DEFAULT  0x00440000UL
#define PART_FLAG_MAGIC     0x42475057UL  /* "BGPW"                         */

#define FLASH_SECTOR_SZ     0x1000UL      /* 4 KB erase unit                 */

/* ── Runtime flash layout (detected by DualPart_Init) ───────────────────── */
typedef struct {
    uint32_t flash_capacity;   /* Total flash size in bytes                */
    uint32_t part_a_usable;    /* Usable A partition size                  */
    uint32_t part_b_usable;    /* Usable B partition size (0 = N/A)        */
    uint32_t part_flag_addr;   /* Actual partition flags address           */
    uint8_t  is_dual;          /* 1 = true dual A/B, 0 = single partition */
} DualPart_Layout_t;

/* Get runtime layout (call DualPart_Init first) */
const DualPart_Layout_t *DualPart_GetLayout(void);

/* Initialize runtime layout from actual flash capacity */
void DualPart_Init(void);

/* ── Firmware validity signature ─────────────────────────────────────────── */
/* Vector table = (9 exception + 32 HW) × 4B = 0xA4 bytes.
 * .stub_section at partition_base + 0xA4 holds FW_VALID_MAGIC
 * to distinguish valid firmware from blank flash.                           */
#define FW_VALID_MAGIC          0x42475046UL  /* "BGPF"                      */
#define FW_VALID_MAGIC_OFFSET   0x000000A4UL

/* ── Boot failure threshold ──────────────────────────────────────────────── */
#define BOOT_FAIL_MAX       3

/* ── Partition flag structure (stored at PART_FLAG_ADDR) ─────────────────── */
/* CRC32 (IEEE 802.3) covers all fields EXCEPT the crc32 field itself.       */
typedef struct {
    uint32_t magic;           /* Must equal PART_FLAG_MAGIC                  */
    uint8_t  active_part;     /* 0 = A is active, 1 = B is active           */
    uint8_t  reserved1;       /* (was upgrade_pending — no longer used)      */
    uint8_t  boot_fail_cnt;   /* Incremented before each jump; reset on OK  */
    uint8_t  reserved2;
    uint32_t crc32;           /* CRC32 of preceding 8 bytes                  */
} PartFlag_t;

/* ── Protocol constants (shared by USB CDC and BLE OTA engines) ──────────── */
#define UPG_SOF             0xAAU
#define UPG_VERSION         0x04U   /* v4: runtime partition capability query */
#define UPG_MAX_CHUNK       256U

/* Commands: Host → Device */
#define CMD_SYNC            0x01U
#define CMD_START           0x02U
#define CMD_DATA            0x03U
#define CMD_FINISH          0x04U
#define CMD_JUMP            0x05U
#define CMD_ERASE           0x06U
#define CMD_QUERY_INFO      0x07U
#define CMD_SET_PART        0x08U
#define CMD_REBOOT          0x09U
#define CMD_ENTER_BOOT      0x0BU  /* APP → bootloader: reboot & stay in BL */

/* Responses */
#define RSP_ACK             0xA1U
#define RSP_NACK            0xA2U

/* NACK error codes */
#define UPG_ERR_CRC         0x01U
#define UPG_ERR_FLASH       0x02U
#define UPG_ERR_SIZE        0x03U
#define UPG_ERR_STATE       0x04U
#define UPG_ERR_PARAM       0x05U
#define UPG_ERR_WRONG_PART  0x06U  /* Cannot upgrade while on Partition 2   */

/* ── Device info (CMD_QUERY_INFO ACK payload) ────────────────────────────── */
typedef struct {
    uint8_t  boot_mode;       /* BOOT_MODE_DUAL_AB = 1                      */
    uint8_t  active_part;     /* 0 = A active, 1 = B active                 */
    uint8_t  boot_fail_cnt;
    uint8_t  protocol_ver;    /* UPG_VERSION (0x04)                         */
    uint32_t part_a_base;
    uint32_t part_a_size;
    uint32_t part_b_base;
    uint32_t part_b_size;
} DevInfo_t;

#define BOOT_MODE_DUAL_AB  1   /* A/B dual-partition with bootloader       */
#define BOOT_MODE_SINGLE   0   /* Single partition (no dual A/B)           */

/* ── Burn flag (Flash, one-time bootloader stay request) ──
 * APP writes BURN_FLAG_MAGIC to BURN_FLAG_ADDR in Flash before rebooting
 * to request bootloader stay.  Bootloader checks at startup, erases it,
 * and stays in upgrade mode.  One-time: if user just reboots without
 * upgrading, the flag is already cleared and bootloader jumps to APP. */
#define BURN_FLAG_ADDR      0x0003F000UL  /* Last sector of bootloader area */
#define BURN_FLAG_MAGIC     0x4F4F5442UL  /* "BOOT" in little-endian       */
#define BURN_FLAG_SECTOR    (BURN_FLAG_ADDR / 4096U)

/* Backward-compatible macro: use runtime layout instead when possible */
#define PART_FLAG_ADDR  (DualPart_GetLayout()->part_flag_addr)

/* ── Partition flag helpers (implemented in boot_decision.c) ─────────────── */
int  PartFlag_Read(PartFlag_t *flag);
int  PartFlag_Write(const PartFlag_t *flag);
void PartFlag_Default(PartFlag_t *flag);

/* ── Boot decision (implemented in boot_decision.c) ──────────────────────── */
/**
 * @brief  Check partition flags and jump to Partition B if active.
 *         Call VERY EARLY in main(), before FreeRTOS / peripherals.
 *         With bootloader, this is called AFTER bootloader has already jumped.
 *         It handles the case where APP needs to verify its own partition.
 */
void Boot_CheckAndJump(void);

/**
 * @brief  Confirm this boot was successful (reset boot_fail_cnt to 0).
 *         Call once early in app startup after basic HW init.
 */
void Boot_ConfirmSuccess(void);

/**
 * @brief  Returns 1 if currently running from Partition B (via remap).
 *         Used by upgrade engine to refuse writes when on Partition B.
 */
int Boot_IsRunningPart2(void);

#endif /* __DUAL_PARTITION_H__ */
