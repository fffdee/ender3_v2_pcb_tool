/**
 * @file  app_upgrade.c
 * @brief Firmware upgrade engine — dual/single partition scheme.
 *
 * Handles USB CDC and BLE OTA upgrade channels through a unified state machine.
 * Both channels feed packets into the same engine; only one upgrade at a time.
 *
 * Dual-partition (8 MB flash): Upgrade writes to Partition B, then switches.
 * Single-partition (2 MB flash): Upgrade overwrites Partition A directly.
 */

#include "app_upgrade.h"
#include "dual_partition.h"
#include "spi_flash.h"
#include "debug.h"
#include "reset.h"
#include <string.h>

/* ── CRC32 utilities ─────────────────────────────────────────────────────── */
static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void crc32_init_table(void)
{
    uint32_t i, j, crc;
    if (crc32_table_init) return;
    crc32_table_init = 1;
    
    for (i = 0; i < 256; i++) {
        crc = i;
        for (j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    if (!crc32_table_init) crc32_init_table();
    
    crc ^= 0xFFFFFFFFUL;
    for (i = 0; i < len; i++) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ buf[i]) & 0xFF];
    }
    crc ^= 0xFFFFFFFFUL;
    return crc;
}

/* ── Upgrade engine state machine ────────────────────────────────────────── */

typedef enum {
    STATE_IDLE = 0,
    STATE_SYNC,
    STATE_QUERY,
    STATE_START,
    STATE_WRITING,
    STATE_FINISH
} UpgradeState_t;

typedef struct {
    UpgradeState_t  state;
    uint16_t        received_len;      /* Bytes in receive buffer */
    uint8_t         rx_buf[256];       /* Protocol receive buffer */
    uint32_t        write_addr;        /* Current write position in Partition 2 */
    uint32_t        total_size;        /* Expected firmware size */
    uint32_t        written_size;      /* Bytes written to flash */
    uint32_t        data_crc;          /* CRC32 of firmware data */
    const UpgradeChannel_t *active_ch; /* Currently active channel (NULL = idle) */
} UpgradeEngine_t;

static UpgradeEngine_t g_engine;

/* ── Helper: Compute upgrade write target based on flash layout ──────────── */
static uint32_t g_upg_base;   /* Write target base address */
static uint32_t g_upg_max;    /* Write target max size */
static uint8_t  g_upg_part;   /* 0=A, 1=B */

static void upgrade_compute_target(void)
{
    const DualPart_Layout_t *layout = DualPart_GetLayout();

    if (layout->is_dual) {
        /* Dual-partition: write to the inactive partition */
        PartFlag_t flags;
        if (PartFlag_Read(&flags) && flags.active_part == 1u) {
            /* Active = B → write to A */
            g_upg_part = 0;
            g_upg_base = PART_A_BASE;
            g_upg_max  = layout->part_a_usable;
        } else {
            /* Active = A → write to B */
            g_upg_part = 1;
            g_upg_base = PART_B_BASE;
            g_upg_max  = layout->part_b_usable;
        }
    } else {
        /* Single-partition: overwrite Partition A directly */
        g_upg_part = 0;
        g_upg_base = PART_A_BASE;
        g_upg_max  = layout->part_a_usable;
        DBG("[UPG] Single-partition mode: upgrade overwrites Part A\n");
    }

    DBG("[UPG] Target: Part %c @ 0x%08X max=%u KB\n",
        g_upg_part ? 'B' : 'A', (unsigned)g_upg_base,
        (unsigned)(g_upg_max / 1024));
}

/* ── Helper: Check which partition is currently running ──────────────────── */
static uint8_t get_running_partition(void)
{
    PartFlag_t flags;
    if (PartFlag_Read(&flags) != 0) {
        return flags.active_part;
    }
    /* Default: assume Partition A */
    return 0;
}

/* ── Helper: Check if upgrade target has valid firmware ──────────────────── */
static int is_target_firmware_valid(void)
{
    /* Internal flash is memory-mapped — read magic directly */
    volatile const uint32_t *magic_ptr =
        (volatile const uint32_t *)(g_upg_base + FW_VALID_MAGIC_OFFSET);
    return (*magic_ptr == FW_VALID_MAGIC) ? 1 : 0;
}

/* ── Protocol: Send ACK ──────────────────────────────────────────────────── */
static void send_ack(const UpgradeChannel_t *ch, uint8_t data)
{
    uint8_t buf[3];
    buf[0] = UPG_SOF;
    buf[1] = RSP_ACK;
    buf[2] = data;
    DBG("[UPG] send_ack: 0x%02X 0x%02X 0x%02X, tx_write=%p\n",
        buf[0], buf[1], buf[2], (void*)(unsigned long)ch->tx_write);
    ch->tx_write(buf, 3);
    DBG("[UPG] send_ack: done\n");
}

/* ── Protocol: Send NACK ─────────────────────────────────────────────────── */
static void send_nack(const UpgradeChannel_t *ch, uint8_t error_code)
{
    uint8_t buf[3];
    buf[0] = UPG_SOF;
    buf[1] = RSP_NACK;
    buf[2] = error_code;
    ch->tx_write(buf, 3);
}

/* ── Protocol: Handle CMD_SYNC ───────────────────────────────────────────── */
static void handle_sync(const UpgradeChannel_t *ch)
{
    DBG("[UPG] handle_sync: tx_write=%p\n", (void*)(unsigned long)ch->tx_write);
    g_engine.state = STATE_SYNC;
    send_ack(ch, UPG_VERSION);
    DBG("[UPG] handle_sync: ACK sent\n");
}

/* ── Protocol: Handle CMD_QUERY_INFO ─────────────────────────────────────── */
static void handle_query(const UpgradeChannel_t *ch)
{
    DevInfo_t info;
    uint8_t buf[sizeof(info) + 3];
    PartFlag_t flags;
    const DualPart_Layout_t *layout = DualPart_GetLayout();

    memset(&info, 0, sizeof(info));
    info.boot_mode = layout->is_dual ? BOOT_MODE_DUAL_AB : BOOT_MODE_SINGLE;
    info.active_part = get_running_partition();

    /* Read current boot_fail_cnt */
    if (PartFlag_Read(&flags) != 0) {
        info.boot_fail_cnt = flags.boot_fail_cnt;
    }

    info.protocol_ver = UPG_VERSION;
    info.part_a_base = PART_A_BASE;
    info.part_a_size = layout->part_a_usable;
    info.part_b_base = PART_B_BASE;
    info.part_b_size = layout->part_b_usable;

    buf[0] = UPG_SOF;
    buf[1] = RSP_ACK;
    memcpy(&buf[2], &info, sizeof(info));

    ch->tx_write(buf, 2 + sizeof(info));
    g_engine.state = STATE_QUERY;
}

/* ── Protocol: Handle CMD_START ──────────────────────────────────────────── */
static void handle_start(const UpgradeChannel_t *ch, const uint8_t *pkt, uint16_t len)
{
    uint32_t fw_size;

    /* Payload: [1:SOF][1:CMD][4:size] = 6 bytes minimum */
    if (len < 6) {
        send_nack(ch, UPG_ERR_PARAM);
        return;
    }

    /* Extract firmware size (big-endian) */
    fw_size = ((uint32_t)pkt[2] << 24) |
              ((uint32_t)pkt[3] << 16) |
              ((uint32_t)pkt[4] << 8) |
              ((uint32_t)pkt[5]);

    /* Compute upgrade target (depends on flash layout) */
    upgrade_compute_target();

    /* Validate size against upgrade target capacity */
    if (fw_size == 0 || fw_size > g_upg_max) {
        send_nack(ch, UPG_ERR_SIZE);
        return;
    }

    /* In dual-partition mode, refuse if running the same partition as target */
    if (DualPart_GetLayout()->is_dual && get_running_partition() == g_upg_part) {
        send_nack(ch, UPG_ERR_WRONG_PART);
        return;
    }

    /* Erase target partition area */
    if (FlashErase(g_upg_base, fw_size) != FLASH_NONE_ERR) {
        send_nack(ch, UPG_ERR_FLASH);
        return;
    }

    /* Initialize state for firmware writing */
    g_engine.state = STATE_WRITING;
    g_engine.active_ch = ch;
    g_engine.write_addr = g_upg_base;
    g_engine.total_size = fw_size;
    g_engine.written_size = 0;
    g_engine.data_crc = 0;

    send_ack(ch, 0);
}

/* ── Protocol: Handle CMD_DATA ───────────────────────────────────────────── */
static void handle_data(const UpgradeChannel_t *ch, const uint8_t *pkt, uint16_t len)
{
    uint16_t chunk_len;
    
    if (g_engine.state != STATE_WRITING) {
        send_nack(ch, UPG_ERR_STATE);
        return;
    }
    
    if (len < 3) {  /* [SOF][CMD][data...] */
        send_nack(ch, UPG_ERR_PARAM);
        return;
    }
    
    chunk_len = len - 2;  /* Exclude SOF and CMD */
    
    /* Check if we'd exceed total size */
    if (g_engine.written_size + chunk_len > g_engine.total_size) {
        send_nack(ch, UPG_ERR_SIZE);
        return;
    }
    
    /* Write chunk to internal flash (IsSuspend=0: no audio IRQ suspend needed here) */
    if (SpiFlashWrite(g_engine.write_addr, (uint8_t *)&pkt[2],
                     chunk_len, 0) != FLASH_NONE_ERR) {
        send_nack(ch, UPG_ERR_FLASH);
        return;
    }
    
    /* Update state */
    g_engine.write_addr += chunk_len;
    g_engine.written_size += chunk_len;
    g_engine.data_crc = crc32_update(g_engine.data_crc, &pkt[2], chunk_len);
    
    send_ack(ch, 0);
}

/* ── Protocol: Handle CMD_FINISH ─────────────────────────────────────────── */
static void handle_finish(const UpgradeChannel_t *ch, const uint8_t *pkt, uint16_t len)
{
    uint32_t received_crc;
    PartFlag_t flags;
    
    if (g_engine.state != STATE_WRITING) {
        send_nack(ch, UPG_ERR_STATE);
        return;
    }
    
    /* Payload: [1:SOF][1:CMD][4:crc] = 6 bytes minimum */
    if (len < 6) {
        send_nack(ch, UPG_ERR_PARAM);
        return;
    }
    
    /* Extract CRC32 (big-endian) */
    received_crc = ((uint32_t)pkt[2] << 24) |
                   ((uint32_t)pkt[3] << 16) |
                   ((uint32_t)pkt[4] << 8) |
                   ((uint32_t)pkt[5]);
    
    /* Verify CRC */
    if (g_engine.data_crc != received_crc) {
        send_nack(ch, UPG_ERR_CRC);
        return;
    }
    
    /* Verify firmware has valid magic at upgrade target + FW_VALID_MAGIC_OFFSET */
    if (!is_target_firmware_valid()) {
        send_nack(ch, UPG_ERR_CRC);  /* Not a valid magic, treat as invalid */
        return;
    }

    /* Update partition flags (only in dual-partition mode) */
    if (DualPart_GetLayout()->is_dual) {
        /* Dual-partition: switch active partition to the upgrade target */
        if (PartFlag_Read(&flags) == 0) {
            PartFlag_Default(&flags);
        }
        flags.active_part = g_upg_part;
        flags.reserved1 = 0;
        flags.boot_fail_cnt = 1;     /* Will be reset on successful boot */

        if (PartFlag_Write(&flags) != 0) {
            send_nack(ch, UPG_ERR_FLASH);
            return;
        }
    } else {
        /* Single-partition: no partition switch needed, just confirm valid firmware */
        DBG("[UPG] Single-partition: firmware written to Part A, no partition switch\n");
    }
    
    send_ack(ch, 0);
    
    /* Transition to FINISH state (app should call Boot_CheckAndJump() / reboot soon) */
    g_engine.state = STATE_FINISH;
    g_engine.active_ch = NULL;
}

/* ── Protocol: Handle CMD_ENTER_BOOT ─────────────────────────────────────── */
static void handle_enter_boot(const UpgradeChannel_t *ch)
{
    uint32_t magic = BURN_FLAG_MAGIC;

    DBG("[UPG] CMD_ENTER_BOOT: writing burn flag to Flash and rebooting\n");
    send_ack(ch, 0);

    /* Small delay to ensure ACK is sent before Flash operation */
    {
        volatile uint32_t delay;
        for (delay = 0; delay < 50000; delay++) { ; }
    }

    /* Write burn flag to Flash (one-time, bootloader will erase it) */
    SpiFlashIOCtrl(IOCTL_FLASH_UNPROTECT, "\x35\xBA\x69", 3);
    SpiFlashErase(SECTOR_ERASE, BURN_FLAG_SECTOR, 1);
    SpiFlashWrite(BURN_FLAG_ADDR, (uint8_t *)&magic, sizeof(magic), 100);
    DBG("[UPG] Burn flag written @0x%08X = 0x%08X\n",
        (unsigned)BURN_FLAG_ADDR, (unsigned)magic);

    /* Small delay to ensure Flash write completes */
    {
        volatile uint32_t delay;
        for (delay = 0; delay < 50000; delay++) { ; }
    }

    Reset_McuSystem();
    /* Never reaches here */
}

/* ── Protocol: Dispatch command ──────────────────────────────────────────── */
static void dispatch_command(const UpgradeChannel_t *ch, const uint8_t *pkt, uint16_t len)
{
    uint8_t cmd;
    
    if (len < 2) {
        DBG("[UPG] dispatch: len=%u < 2, skip\n", (unsigned)len);
        return;  /* Too short */
    }
    
    if (pkt[0] != UPG_SOF) {
        DBG("[UPG] dispatch: pkt[0]=0x%02X != SOF, skip\n", pkt[0]);
        return;  /* Invalid SOF */
    }
    
    cmd = pkt[1];
    DBG("[UPG] dispatch: cmd=0x%02X, len=%u, tx_write=%p\n",
        cmd, (unsigned)len, (void*)(unsigned long)ch->tx_write);
    
    switch (cmd) {
        case CMD_SYNC:
            handle_sync(ch);
            break;
        case CMD_QUERY_INFO:
            handle_query(ch);
            break;
        case CMD_START:
            handle_start(ch, pkt, len);
            break;
        case CMD_DATA:
            handle_data(ch, pkt, len);
            break;
        case CMD_FINISH:
            handle_finish(ch, pkt, len);
            break;
        case CMD_ENTER_BOOT:
            handle_enter_boot(ch);
            break;
    /* Other commands not implemented in this phase */
        default:
            send_nack(ch, UPG_ERR_PARAM);
            break;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ════════════════════════════════════════════════════════════════════════════ */

void App_Upgrade_Init(void)
{
    memset(&g_engine, 0, sizeof(g_engine));
    g_engine.state = STATE_IDLE;
    crc32_init_table();
}

void App_Upgrade_ProcessChannel(const UpgradeChannel_t *ch)
{
    uint16_t available;
    uint16_t rx_len;
    
    if (!ch) {
        return;
    }
    
    /* Get number of bytes available to read */
    available = ch->rx_available();
    if (available == 0) {
        return;
    }
    
    /* Limit read to one packet at a time.
     * rx_buf is 256 bytes; reserve 2 for SOF+CMD header so data payload
     * fits within the buffer. Using UPG_MAX_CHUNK+2 (=258) would overflow! */
    if (available > (uint16_t)(sizeof(g_engine.rx_buf) - 2u) + 2u) {
        available = (uint16_t)(sizeof(g_engine.rx_buf));
    }
    
    /* Read data into buffer */
    rx_len = ch->rx_read(g_engine.rx_buf, available);
    if (rx_len == 0) {
        return;
    }
    
    /* Dispatch command */
    dispatch_command(ch, g_engine.rx_buf, rx_len);
}

void App_Upgrade_InjectRaw(uint8_t ch_id, const uint8_t *buf, uint16_t len,
                           void (*tx_fn)(const uint8_t *data, uint16_t len))
{
    /* Static so the pointer remains valid after this function returns.
     * (g_engine.active_ch may point here across multiple InjectRaw calls.) */
    static UpgradeChannel_t s_inject_ch;
    DBG("[UPG] InjectRaw: ch=%u len=%u tx_fn=%p\n",
        ch_id, (unsigned)len, (void*)(unsigned long)tx_fn);
    memset(&s_inject_ch, 0, sizeof(s_inject_ch));
    s_inject_ch.id       = ch_id;
    s_inject_ch.tx_write = tx_fn;
    dispatch_command(&s_inject_ch, buf, len);
}

int App_Upgrade_IsActive(void)
{
    return (g_engine.state != STATE_IDLE && g_engine.active_ch != NULL);
}

int App_Upgrade_IsFinished(void)
{
    return (g_engine.state == STATE_FINISH);
}

/* ════════════════════════════════════════════════════════════════════════════
 * BLE OTA BACKWARD-COMPATIBLE API
 * ════════════════════════════════════════════════════════════════════════════ */

/* BLE-specific state */
static struct {
    UpgradeChannel_t ch;
    void (*send_fn)(const uint8_t *data, uint16_t len); /* app BLE send callback */
    uint8_t rx_buf[256];
    uint16_t rx_pos;
} g_ble_ota;

static uint16_t ble_rx_read(uint8_t *buf, uint16_t maxLen)
{
    uint16_t to_copy = g_ble_ota.rx_pos;
    if (to_copy > maxLen) {
        to_copy = maxLen;
    }
    if (to_copy > 0) {
        memcpy(buf, g_ble_ota.rx_buf, to_copy);
        g_ble_ota.rx_pos = 0;
    }
    return to_copy;
}

static void ble_tx_write(const uint8_t *buf, uint16_t len)
{
    if (g_ble_ota.send_fn) {
        g_ble_ota.send_fn(buf, len);
    }
}

static int ble_rx_available(void)
{
    return (int)g_ble_ota.rx_pos;
}

void App_OTA_Init(void (*send_fn)(const uint8_t *data, uint16_t len))
{
    memset(&g_ble_ota, 0, sizeof(g_ble_ota));
    g_ble_ota.send_fn         = send_fn;
    g_ble_ota.ch.id           = UPG_CH_BLE;
    g_ble_ota.ch.rx_read      = ble_rx_read;
    g_ble_ota.ch.tx_write     = ble_tx_write;
    g_ble_ota.ch.rx_available = ble_rx_available;
}

void App_OTA_OnData(const uint8_t *data, uint16_t len)
{
    /* Buffer incoming BLE bytes until process() is called */
    uint16_t to_copy = len;
    if (g_ble_ota.rx_pos + to_copy > sizeof(g_ble_ota.rx_buf)) {
        to_copy = sizeof(g_ble_ota.rx_buf) - g_ble_ota.rx_pos;
    }
    if (to_copy > 0) {
        memcpy(&g_ble_ota.rx_buf[g_ble_ota.rx_pos], data, to_copy);
        g_ble_ota.rx_pos += to_copy;
    }
}

void App_OTA_Process(void)
{
    App_Upgrade_ProcessChannel(&g_ble_ota.ch);
}

void App_ConfirmBootSuccess(void)
{
    Boot_ConfirmSuccess();
}
