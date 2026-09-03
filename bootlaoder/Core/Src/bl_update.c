#include "bl_update.h"
#include "bl_config.h"
#include "bl_flash.h"
#include "bl_log.h"
#include "fatfs.h"
#include "string.h"

static uint32_t s_written;
static uint32_t s_total;
static int s_sd_ok;
static int s_use_sd;
static int s_file_open;
static uint8_t s_boot_fail;

static int sd_mount(void)
{
    if (f_mount(&SDFatFS, SDPath, 1) == FR_OK) {
        return 1;
    }
    return 0;
}

static void sd_unmount(void)
{
    (void)f_mount(NULL, SDPath, 0);
}

static int file_write_at(uint32_t offset, const uint8_t *data, uint32_t len)
{
    UINT bw = 0;

    if (!s_file_open) {
        return -1;
    }
    if (f_lseek(&SDFile, offset) != FR_OK) {
        return -1;
    }
    if (f_write(&SDFile, data, len, &bw) != FR_OK || bw != len) {
        return -1;
    }
    return 0;
}

static int apply_file_to_app(FIL *fp, uint32_t size)
{
    uint8_t buf[256];
    uint32_t off = 0;
    UINT br;

    if (bl_flash_erase_app() != 0) {
        return -1;
    }
    if (f_lseek(fp, 0) != FR_OK) {
        return -1;
    }
    while (off < size) {
        uint32_t chunk = size - off;
        if (chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        if (f_read(fp, buf, chunk, &br) != FR_OK || br != chunk) {
            return -1;
        }
        if (bl_flash_write(off, buf, chunk) != 0) {
            return -1;
        }
        off += chunk;
    }
    return 0;
}

void bl_update_init(void)
{
    bl_cfg_t cfg;

    s_written = 0;
    s_total = 0;
    s_use_sd = 0;
    s_file_open = 0;
    s_sd_ok = sd_mount();
    bl_cfg_load(&cfg);
    s_boot_fail = cfg.boot_fail_cnt;
}

int bl_sd_present(void)
{
    return s_sd_ok;
}

int bl_sd_try_upgrade(void)
{
    FRESULT fr;
    uint32_t size;
    bl_cfg_t cfg;
    uint8_t hdr[8];
    UINT br;
    uint32_t sp;
    uint32_t pc;

    if (!s_sd_ok) {
        bl_log("SD: no card");
        return 0;
    }

    fr = f_open(&SDFile, BL_UPGRADE_NAME, FA_READ);
    if (fr != FR_OK) {
        bl_log("SD: no UPGRADE.BIN");
        return 0;
    }

    size = (uint32_t)f_size(&SDFile);
    bl_cfg_load(&cfg);
    bl_log_u32("SD: file size ", size);

    if ((size == 0u) || (size > BL_APP_SIZE)) {
        (void)f_close(&SDFile);
        bl_log("SD: bad size");
        return 0;
    }

    /* UART 未完成的文件：expected_size 已记录且尚未写满，跳过 */
    if ((cfg.expected_size != 0u) && (cfg.expected_size != 0xFFFFFFFFu) &&
        (size != cfg.expected_size)) {
        (void)f_close(&SDFile);
        bl_log("SD: incomplete UART file, skip");
        return 0;
    }

    if (f_read(&SDFile, hdr, 8, &br) != FR_OK || br != 8) {
        (void)f_close(&SDFile);
        return 0;
    }
    memcpy(&sp, &hdr[0], 4);
    memcpy(&pc, &hdr[4], 4);
    if ((sp < BL_SRAM_BASE) || (sp > (BL_SRAM_BASE + BL_SRAM_SIZE)) ||
        (pc < BL_APP_BASE) || (pc >= 0x08080000u)) {
        (void)f_close(&SDFile);
        bl_log("SD: vector invalid");
        return 0;
    }

    bl_log("SD: upgrading from UPGRADE.BIN");
    (void)bl_cfg_set_pending(1, size);
    if (apply_file_to_app(&SDFile, size) != 0) {
        (void)f_close(&SDFile);
        bl_log("SD: write app failed");
        return -1;
    }
    (void)f_close(&SDFile);
    (void)f_unlink(BL_UPGRADE_NAME);
    (void)bl_cfg_set_pending(0, 0);
    bl_log("SD: upgrade done, reset");
    HAL_Delay(50);
    NVIC_SystemReset();
    return 1;
}

int bl_upg_start(uint32_t total)
{
    FRESULT fr;

    if ((total == 0u) || (total > BL_APP_SIZE)) {
        return BL_ERR_SIZE_OVERFLOW;
    }

    s_total = total;
    s_written = 0;
    /* UART 下载（串口直连 / 无线桥接）一律直写 Flash，不落 SD。
     * SD 卡仍用于"放 UPGRADE.BIN 自动升级"(bl_sd_try_upgrade)，不受影响。 */
    s_use_sd = 0;

    if (bl_cfg_set_pending(1, total) != 0) {
        return BL_ERR_FLASH;
    }

    if (s_use_sd) {
        if (s_file_open) {
            (void)f_close(&SDFile);
            s_file_open = 0;
        }
        fr = f_open(&SDFile, BL_UPGRADE_NAME, FA_WRITE | FA_CREATE_ALWAYS);
        if (fr != FR_OK) {
            bl_log("UART: SD create fail, fallback flash");
            s_use_sd = 0;
            if (bl_flash_erase_app() != 0) {
                return BL_ERR_FLASH;
            }
            bl_log("UART: download to app");
        } else {
            s_file_open = 1;
            bl_log("UART: download to SD then app");
        }
    } else {
        bl_log("UART: download to app");
        if (bl_flash_erase_app() != 0) {
            return BL_ERR_FLASH;
        }
    }
    return 0;
}

int bl_upg_data(uint32_t offset, const uint8_t *data, uint32_t len)
{
    uint32_t end = offset + len;

    if ((len == 0u) || (end > BL_APP_SIZE) || (s_total != 0u && end > s_total)) {
        return BL_ERR_SIZE_OVERFLOW;
    }

    if (s_use_sd) {
        if (file_write_at(offset, data, len) != 0) {
            return BL_ERR_FLASH;
        }
    } else {
        if (bl_flash_write(offset, data, len) != 0) {
            return BL_ERR_FLASH;
        }
    }

    if (end > s_written) {
        s_written = end;
    }
    return 0;
}

int bl_upg_finish(uint32_t total)
{
    if ((total == 0u) || (total != s_total) || (s_written != total)) {
        return BL_ERR_STATE_INVALID;
    }

    if (s_use_sd && s_file_open) {
        if (apply_file_to_app(&SDFile, total) != 0) {
            (void)f_close(&SDFile);
            s_file_open = 0;
            return BL_ERR_FLASH;
        }
        (void)f_sync(&SDFile);
        (void)f_close(&SDFile);
        s_file_open = 0;
        (void)f_unlink(BL_UPGRADE_NAME);
    }

    if (!bl_app_vector_valid()) {
        return BL_ERR_STATE_INVALID;
    }

    if (bl_cfg_set_pending(0, 0) != 0) {
        return BL_ERR_FLASH;
    }
    bl_log("UART: finish ok");
    return 0;
}

int bl_upg_erase(void)
{
    if (bl_cfg_set_pending(1, 0) != 0) {
        return BL_ERR_FLASH;
    }
    if (bl_flash_erase_app() != 0) {
        return BL_ERR_FLASH;
    }
    s_written = 0;
    s_total = 0;
    return 0;
}

int bl_upg_can_jump(void)
{
    bl_cfg_t cfg;

    bl_cfg_load(&cfg);
    if (!bl_app_vector_valid()) {
        return 0;
    }
    if (cfg.pending == BL_MAGIC_PEND) {
        /* 半截升级拒绝跳转；向量有效则允许救砖 */
        if (s_written != 0u && s_total != 0u && s_written != s_total) {
            return 0;
        }
    }
    return 1;
}

void bl_upg_on_jump_ok(void)
{
    bl_cfg_t cfg;

    bl_cfg_load(&cfg);
    if (cfg.pending == BL_MAGIC_PEND) {
        (void)bl_cfg_set_pending(0, 0);
    }
}

uint8_t bl_upg_boot_fail_cnt(void)
{
    return s_boot_fail;
}

uint32_t bl_upg_written(void)
{
    return s_written;
}
