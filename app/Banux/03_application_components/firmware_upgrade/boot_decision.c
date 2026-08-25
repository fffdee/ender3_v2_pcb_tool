/**
 * @file  boot_decision.c
 * @brief Boot decision logic for bootloader + dual-partition scheme.
 *
 * With a separate bootloader, the bootloader has already decided which
 * partition to run and performed the address remap before jumping here.
 * This module provides:
 *   1. DualPart_Init() — detect flash capacity and compute layout.
 *   2. Boot_CheckAndJump() — detect which partition we're running on.
 *   3. Boot_ConfirmSuccess() — resets boot_fail_cnt after successful startup.
 *   4. Boot_IsRunningPart2() — detects if running from Partition B via remap.
 *   5. Partition flag read/write helpers (use dynamic address).
 */

#include <string.h>
#include <nds32_intrinsic.h>
#include "dual_partition.h"
#include "spi_flash.h"
#include "watchdog.h"
#include "remap.h"
#include "debug.h"

/* =========================================================================
 * Runtime flash layout (detected by DualPart_Init)
 * ========================================================================= */
static DualPart_Layout_t g_layout;

const DualPart_Layout_t *DualPart_GetLayout(void)
{
    return &g_layout;
}

void DualPart_Init(void)
{
    /* Use internal ROM capacity (no external NOR Flash detection) */
    g_layout.flash_capacity = INTERNAL_ROM_CAPACITY;

    DBG("[BOOT] Internal ROM: Capacity=%u bytes (%u KB)\n",
        (unsigned)g_layout.flash_capacity,
        (unsigned)(g_layout.flash_capacity / 1024));

    /* Compute PartFlag address: use last 4KB sector of flash */
    if (g_layout.flash_capacity >= PART_FLAG_ADDR_DEFAULT + FLASH_SECTOR_SZ) {
        g_layout.part_flag_addr = PART_FLAG_ADDR_DEFAULT;
    } else {
        g_layout.part_flag_addr = (g_layout.flash_capacity - FLASH_SECTOR_SZ)
                                  & ~(FLASH_SECTOR_SZ - 1u);
        DBG("[BOOT] PartFlag moved: 0x%08X -> 0x%08X\n",
            (unsigned)PART_FLAG_ADDR_DEFAULT,
            (unsigned)g_layout.part_flag_addr);
    }

    /* Compute usable Partition A size (clamped to actual flash boundary,
     * minus PartFlag sector if it falls inside A) */
    if (g_layout.flash_capacity >= PART_A_BASE + PART_A_SIZE &&
        g_layout.part_flag_addr >= PART_A_BASE + PART_A_SIZE) {
        g_layout.part_a_usable = PART_A_SIZE;
    } else {
        uint32_t a_end = g_layout.flash_capacity;
        if (g_layout.part_flag_addr >= PART_A_BASE &&
            g_layout.part_flag_addr < a_end) {
            a_end = g_layout.part_flag_addr;
        }
        g_layout.part_a_usable = (a_end > PART_A_BASE) ? a_end - PART_A_BASE : 0;
    }

    /* Compute usable Partition B size */
    if (g_layout.flash_capacity >= PART_B_BASE + PART_B_SIZE &&
        g_layout.part_flag_addr >= PART_B_BASE + PART_B_SIZE) {
        g_layout.part_b_usable = PART_B_SIZE;
    } else if (g_layout.flash_capacity > PART_B_BASE) {
        uint32_t b_end = g_layout.flash_capacity;
        if (g_layout.part_flag_addr >= PART_B_BASE &&
            g_layout.part_flag_addr < b_end) {
            b_end = g_layout.part_flag_addr;
        }
        g_layout.part_b_usable = b_end - PART_B_BASE;
    } else {
        g_layout.part_b_usable = 0;
    }

    /* Determine dual-partition availability */
    g_layout.is_dual = (g_layout.part_a_usable > 0 && g_layout.part_b_usable > 0) ? 1 : 0;

    DBG("[BOOT] Layout: %s A=%uKB B=%uKB flags@0x%08X\n",
        g_layout.is_dual ? "DUAL" : "SINGLE",
        (unsigned)(g_layout.part_a_usable / 1024),
        (unsigned)(g_layout.part_b_usable / 1024),
        (unsigned)g_layout.part_flag_addr);
}

/* =========================================================================
 * CRC32 (IEEE 802.3, poly = 0xEDB88320) — for partition flags
 * ========================================================================= */
static uint32_t crc32_calc(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i, j;
    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i];
        for (j = 0; j < 8u; j++)
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFUL;
}

/* =========================================================================
 * Partition flag helpers
 * ========================================================================= */
static void part_flag_seal(PartFlag_t *f)
{
    f->magic = PART_FLAG_MAGIC;
    f->crc32 = crc32_calc((const uint8_t *)f,
                          sizeof(PartFlag_t) - sizeof(uint32_t));
}

static int part_flag_valid(const PartFlag_t *f)
{
    if (f->magic != PART_FLAG_MAGIC) return 0;
    return (crc32_calc((const uint8_t *)f,
                       sizeof(PartFlag_t) - sizeof(uint32_t))
            == f->crc32) ? 1 : 0;
}

int PartFlag_Read(PartFlag_t *flag)
{
    memcpy(flag, (const void *)g_layout.part_flag_addr, sizeof(PartFlag_t));
    return part_flag_valid(flag) ? 1 : 0;
}

void PartFlag_Default(PartFlag_t *flag)
{
    memset(flag, 0, sizeof(PartFlag_t));
    flag->active_part     = 0;   /* boot Partition A */
    flag->reserved1       = 0;
    flag->boot_fail_cnt   = 0;
    flag->reserved2       = 0;
    part_flag_seal(flag);
}

int PartFlag_Write(const PartFlag_t *flag)
{
    PartFlag_t tmp;
    memcpy(&tmp, flag, sizeof(PartFlag_t));
    part_flag_seal(&tmp);

    if (FlashErase(g_layout.part_flag_addr, FLASH_SECTOR_SZ) != FLASH_NONE_ERR)
        return 0;
    return (SpiFlashWrite(g_layout.part_flag_addr, (uint8_t *)&tmp,
                         sizeof(PartFlag_t), 0) == FLASH_NONE_ERR) ? 1 : 0;
}

/* =========================================================================
 * Runtime partition tracking
 * ========================================================================= */
static int g_running_part2 = 0;

int Boot_IsRunningPart2(void)
{
    return g_running_part2;
}

/* =========================================================================
 * Boot_JumpTo — jump to an address (never returns)
 * ========================================================================= */
static void Boot_JumpTo(uint32_t addr)
{
    typedef void (*Entry_t)(void);
    Entry_t entry;

    WDG_Disable();
    __nds32__setgie_dis();
    entry = (Entry_t)addr;
    entry();
    while (1);
}

/* =========================================================================
 * Boot_CheckAndJump — with bootloader, this is handled by bootloader.
 * The APP just needs to detect which partition it's running on.
 * ========================================================================= */
void Boot_CheckAndJump(void)
{
    PartFlag_t flag;

    /* With bootloader, the jump decision is already made.
     * Just detect which partition we're running on. */
    if (PartFlag_Read(&flag) && flag.active_part == 1) {
        g_running_part2 = 1;
        DBG("[BOOT] Running from Partition B (via remap)\n");
    } else {
        g_running_part2 = 0;
        DBG("[BOOT] Running from Partition A\n");
    }
}

/* =========================================================================
 * Boot_ConfirmSuccess — reset boot_fail_cnt to 0
 * ========================================================================= */
void Boot_ConfirmSuccess(void)
{
    PartFlag_t flag;

    if (!PartFlag_Read(&flag)) {
        /* No valid flags — nothing to confirm. */
        return;
    }

    /* Only need to confirm when running Partition B. */
    if (flag.active_part != 1) {
        return;
    }

    if (flag.boot_fail_cnt == 0) {
        return;   /* Already confirmed. */
    }

    flag.boot_fail_cnt = 0;
    PartFlag_Write(&flag);
    g_running_part2 = 1;
    DBG("[BOOT] Boot success confirmed (Part B, fail_cnt reset)\n");
}
