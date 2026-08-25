/**
 * flash_test.c - 鏂�Flash 椹卞姩鏋舵瀯娴嬭瘯浠ｇ�? * 娴嬭�?flash_bus + flash_devices + flash_nor_w25qxx 鏋舵�? */

#include "flash_test.h"
#include "BG_FlashMgr.h"
#include "flash_bus.h"
#include "flash_devices.h"
#include "flash_nor_w25qxx.h"
#include "flash_nand_w25n02.h"
#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>
#include <stdlib.h>
#include "banux_config.h"
#include "gpio.h"

/*===========================================================================
 * SPI Bus GPIO Diagnostic
 *===========================================================================*/

/**
 * @brief Print SPI bus GPIO states for debugging pin conflicts
 */
static void spi_bus_gpio_diag(void)
{
    uint32_t a_oe  = GPIO_RegGet(GPIO_A_OE);
    uint32_t a_ie  = GPIO_RegGet(GPIO_A_IE);
    uint32_t a_out = GPIO_RegGet(GPIO_A_OUT);
    uint32_t a_in  = GPIO_RegGet(GPIO_A_IN);
    uint32_t b_oe  = GPIO_RegGet(GPIO_B_OE);
    uint32_t b_ie  = GPIO_RegGet(GPIO_B_IE);
    uint32_t b_out = GPIO_RegGet(GPIO_B_OUT);

    DBG("\n--- SPI Bus GPIO Diagnostic ---\n");
    /* NOR CS = A28 */
    DBG("  A28 (NOR CS):  OE=%u IE=%u OUT=%u IN=%u\n",
        (unsigned)((a_oe >> 28) & 1), (unsigned)((a_ie >> 28) & 1),
        (unsigned)((a_out >> 28) & 1), (unsigned)((a_in >> 28) & 1));
#ifdef BANBOX_II
    /* NAND CS = A29 */
    DBG("  A29 (NAND CS): OE=%u IE=%u OUT=%u IN=%u\n",
        (unsigned)((a_oe >> 29) & 1), (unsigned)((a_ie >> 29) & 1),
        (unsigned)((a_out >> 29) & 1), (unsigned)((a_in >> 29) & 1));
    /* PSRAM CS = B6 */
    DBG("  B6  (PSRAM CS):OE=%u IE=%u OUT=%u\n",
        (unsigned)((b_oe >> 6) & 1), (unsigned)((b_ie >> 6) & 1),
        (unsigned)((b_out >> 6) & 1));
#endif
    /* SPI data pins: A5=MOSI, A6=CLK, A7=MISO */
    DBG("  A5/A6/A7 (MOSI/CLK/MISO): OE=%u%u%u IE=%u%u%u\n",
        (unsigned)((a_oe >> 5) & 1), (unsigned)((a_oe >> 6) & 1), (unsigned)((a_oe >> 7) & 1),
        (unsigned)((a_ie >> 5) & 1), (unsigned)((a_ie >> 6) & 1), (unsigned)((a_ie >> 7) & 1));
    DBG("---\n");
}

#ifdef NOR_FLASH_TEST
static uint8_t test_write_buffer[512];
static uint8_t test_read_buffer[512];

/**
 * @brief 娴嬭瘯鍗曚釜 Flash 璁惧�? * @param dev 璁惧鎸囬拡
 * @param device_name 璁惧鍚嶇О锛堢敤浜庢墦鍗帮級
 * @return true 娴嬭瘯閫氳繃锛宖alse 娴嬭瘯澶辫触
 */
static bool test_single_flash_device(FlashDevice_t *dev, const char* device_name)
{
    FlashDevInfo_t info;
    uint32_t test_address = 0x1000; /* 浣跨敤绗簩涓�K鎵囧�?*/
    uint16_t i;
    bool result = true;

    DBG("\n========== Testing %s ==========\n", device_name);

    /* 1. 鑾峰彇璁惧淇℃�?*/
    if (dev->ops->get_info(dev, &info) != FLASH_OK) {
        DBG("[FAIL] Failed to get device info\n");
        return false;
    }

    DBG("Device Info:\n");
    DBG("  Manufacturer: 0x%02X\n", info.mfg_id);
    DBG("  Memory Type:  0x%02X\n", info.mem_type);
    DBG("  Device ID:    0x%02X\n", info.dev_id);
    DBG("  Total Size:   %lu bytes\n", (unsigned long)info.total_size);
    DBG("  Page Size:    %lu bytes\n", (unsigned long)info.page_size);
    DBG("  Sector Size:  %lu bytes\n", (unsigned long)info.sector_size);
    DBG("  Block Size:   %lu bytes\n", (unsigned long)info.block_size);

    /* 1b. Read Status Register for diagnostics */
    {
        uint8_t sr = 0xFF;
        if (dev->ops->get_status) {
            dev->ops->get_status(dev, &sr);
        }
        DBG("  Status Reg:   0x%02X (BP=%s%s%s WEL=%u BUSY=%u)\n", sr,
            (sr & 0x10) ? "1" : "0", (sr & 0x08) ? "1" : "0", (sr & 0x04) ? "1" : "0",
            (unsigned)((sr >> 1) & 1), (unsigned)(sr & 1));
    }

    /* 2. 鍑嗗娴嬭瘯鏁版�?*/
    for (i = 0; i < 512; i++) {
        test_write_buffer[i] = (uint8_t)(0xA0 + (i & 0x0F));
    }

    /* 3. 鍗曞瓧鑺傛祴璇�*/
    DBG("\n--- Single Byte Test ---\n");
    test_write_buffer[0] = 0xAA;

    if (FlashDev_EraseSector(dev, test_address) != FLASH_OK) {
        DBG("[FAIL] Sector erase failed\n");
        return false;
    }

    if (FlashDev_Write(dev, test_address, test_write_buffer, 1) != FLASH_OK) {
        DBG("[FAIL] Single byte write failed\n");
        return false;
    }

    if (FlashDev_Read(dev, test_address, test_read_buffer, 1) != FLASH_OK) {
        DBG("[FAIL] Single byte read failed\n");
        return false;
    }

    if (test_write_buffer[0] == test_read_buffer[0]) {
        DBG("[OK] Single byte: wrote 0x%02X, read 0x%02X\n", 
            test_write_buffer[0], test_read_buffer[0]);
    } else {
        DBG("[FAIL] Single byte: wrote 0x%02X, read 0x%02X\n", 
            test_write_buffer[0], test_read_buffer[0]);
        result = false;
    }

    /* 4. 256瀛楄妭椤靛啓鍏ユ祴璇�?/
    DBG("\n--- 256 Byte Page Test ---\n");
    
    if (FlashDev_EraseSector(dev, test_address) != FLASH_OK) {
        DBG("[FAIL] Sector erase failed\n");
        return false;
    }

    if (FlashDev_Write(dev, test_address, test_write_buffer, 256) != FLASH_OK) {
        DBG("[FAIL] 256 byte write failed\n");
        return false;
    }

    memset(test_read_buffer, 0, 256);
    if (FlashDev_Read(dev, test_address, test_read_buffer, 256) != FLASH_OK) {
        DBG("[FAIL] 256 byte read failed\n");
        return false;
    }

    /* 楠岃瘉鏁版嵁 */
    for (i = 0; i < 256; i++) {
        if (test_write_buffer[i] != test_read_buffer[i]) {
            DBG("[FAIL] 256 byte verify failed at offset %d: wrote 0x%02X, read 0x%02X\n",
                i, test_write_buffer[i], test_read_buffer[i]);
            result = false;
            break;
        }
    }
    if (i == 256) {
        DBG("[OK] 256 byte write/read verified\n");
    }

    /* 5. 512瀛楄妭璺ㄩ〉娴嬭瘯 */
    DBG("\n--- 512 Byte Cross-Page Test ---\n");

    if (FlashDev_EraseSector(dev, test_address) != FLASH_OK) {
        DBG("[FAIL] Sector erase failed\n");
        return false;
    }

    if (FlashDev_Write(dev, test_address, test_write_buffer, 512) != FLASH_OK) {
        DBG("[FAIL] 512 byte write failed\n");
        return false;
    }

    memset(test_read_buffer, 0, 512);
    if (FlashDev_Read(dev, test_address, test_read_buffer, 512) != FLASH_OK) {
        DBG("[FAIL] 512 byte read failed\n");
        return false;
    }

    /* 楠岃瘉鏁版嵁 */
    for (i = 0; i < 512; i++) {
        if (test_write_buffer[i] != test_read_buffer[i]) {
            DBG("[FAIL] 512 byte verify failed at offset %d: wrote 0x%02X, read 0x%02X\n",
                i, test_write_buffer[i], test_read_buffer[i]);
            result = false;
            break;
        }
    }
    if (i == 512) {
        DBG("[OK] 512 byte cross-page write/read verified\n");
    }

    return result;
}

/**
 * @brief NOR Flash �������� (���� BANBOX_II ��Ƭ NOR �;ɰ�˫Ƭ NOR)
 */
void FlashNewDriver_Test(void)
{
    bool nor0_result = false;
    FlashDevice_t *dev;

    DBG("\n");
    DBG("**************************************************\n");
    DBG("*       NOR Flash Driver Test                   *\n");
    DBG("**************************************************\n");
    spi_bus_gpio_diag();

    /* NOR Flash #0 (system flash) */
    dev = FlashBus_GetDeviceById(FLASH_DEV_ID_SYSTEM);
    if (dev != NULL) {
        nor0_result = test_single_flash_device(dev, "NOR0 (flash0_sys)");
    } else {
        DBG("[SKIP] NOR0 device not found\n");
    }

#if HW_FLASH1_EN
    {
        bool nor1_result = false;
        dev = FlashBus_GetDeviceById(FLASH_DEV_ID_STORAGE);
        if (dev != NULL) {
            nor1_result = test_single_flash_device(dev, "NOR1 (flash1_stor)");
        } else {
            DBG("[SKIP] NOR1 device not found\n");
        }
        DBG("\n========================================\n");
        DBG("  NOR0: %s  |  NOR1: %s\n",
            nor0_result ? "PASS" : "FAIL",
            nor1_result ? "PASS" : "FAIL");
        DBG("========================================\n");
    }
#else
    DBG("\n========================================\n");
    DBG("  NOR0: %s  (single NOR)\n", nor0_result ? "PASS" : "FAIL");
    DBG("========================================\n");
#endif
}

/**
 * @brief NOR Flash ���ٹ��ܲ���
 */
void FlashNewDriver_QuickTest(void)
{
    FlashDevice_t *dev;
    uint8_t test_data = 0xAA;
    uint8_t read_data = 0;
    uint32_t addr = 0x1000;

    DBG("\n=== NOR Flash Quick Test ===\n");

    /* NOR Flash #0 */
    dev = FlashBus_GetDeviceById(FLASH_DEV_ID_SYSTEM);
    if (dev != NULL) {
        DBG("Testing NOR0 (flash0_sys)...\n");
        FlashDev_EraseSector(dev, addr);
        FlashDev_Write(dev, addr, &test_data, 1);
        FlashDev_Read(dev, addr, &read_data, 1);
        DBG("Wrote: 0x%02X, Read: 0x%02X %s\n",
            test_data, read_data, (test_data == read_data) ? "[OK]" : "[FAIL]");
    } else {
        DBG("[SKIP] NOR0 not found\n");
    }

#if HW_FLASH1_EN
    /* NOR Flash #1 */
    dev = FlashBus_GetDeviceById(FLASH_DEV_ID_STORAGE);
    if (dev != NULL) {
        test_data = 0x55;
        DBG("Testing NOR1 (flash1_stor)...\n");
        FlashDev_EraseSector(dev, addr);
        FlashDev_Write(dev, addr, &test_data, 1);
        FlashDev_Read(dev, addr, &read_data, 1);
        DBG("Wrote: 0x%02X, Read: 0x%02X %s\n",
            test_data, read_data, (test_data == read_data) ? "[OK]" : "[FAIL]");
    } else {
        DBG("[SKIP] NOR1 not found\n");
    }
#endif
}

void NorFlash_SpeedTest(uint32_t test_size_kb, NorTestResult_t *result)
{
    FlashDevice_t *dev;
    uint32_t total_bytes;
    uint32_t pure_write_ms, write_with_erase_ms, read_ms;
    uint32_t t_start, t_end, ticks;
    float    pure_write_kbs, write_with_erase_kbs, read_kbs;
    uint32_t offset;
    uint32_t page_off;
    uint32_t test_addr = 0x10000u; /* start at block 1, avoid boot sector */
    uint32_t chunk     = 256u;     /* one NOR page per iteration */
    uint16_t i;

    if (test_size_kb == 0u) { test_size_kb = 512u; }
    if (test_size_kb > 4096u) { test_size_kb = 4096u; }
    total_bytes = test_size_kb * 1024u;

    DBG("\n=== NOR Speed Test (%lu KB) ===\n", (unsigned long)test_size_kb);

    dev = FlashBus_GetDeviceByName("flash0_sys");
    if (!dev || !dev->initialized) {
        DBG("[NOR Speed] Device not available\n");
        if (result) { result->test_passed = false; }
        return;
    }

    /* fill pattern */
    for (i = 0u; i < (uint16_t)chunk; i++) {
        test_write_buffer[i] = (uint8_t)(0xA5u ^ i);
    }

    /* ---- Step 1: pre-erase entire region (for pure-write test) ---- */
    DBG("  Pre-erasing %lu KB...\n", (unsigned long)test_size_kb);
    for (offset = 0u; offset < total_bytes; offset += 4096u) {
        FlashDev_EraseSector(dev, test_addr + offset);
    }

    /* ---- Step 2: pure write (region already erased, no erase overhead) ---- */
    DBG("  Write [no erase] %lu KB...\n", (unsigned long)test_size_kb);
    t_start = (uint32_t)xTaskGetTickCount();
    for (offset = 0u; offset < total_bytes; offset += chunk) {
        FlashDev_Write(dev, test_addr + offset, test_write_buffer, (uint32_t)chunk);
    }
    t_end         = (uint32_t)xTaskGetTickCount();
    ticks         = (t_end >= t_start) ? (t_end - t_start) : (0xFFFFFFFFu - t_start + t_end + 1u);
    pure_write_ms = ticks * portTICK_PERIOD_MS;
    if (pure_write_ms == 0u) { pure_write_ms = 1u; }
    pure_write_kbs = (float)total_bytes / 1024.0f * 1000.0f / (float)pure_write_ms;

    /* ---- Step 3: erase+write back-to-back (sector erase then fill pages) ---- */
    DBG("  Write [with erase] %lu KB...\n", (unsigned long)test_size_kb);
    t_start = (uint32_t)xTaskGetTickCount();
    for (offset = 0u; offset < total_bytes; offset += 4096u) {
        FlashDev_EraseSector(dev, test_addr + offset);
        for (page_off = 0u; page_off < 4096u; page_off += chunk) {
            FlashDev_Write(dev, test_addr + offset + page_off, test_write_buffer, (uint32_t)chunk);
        }
    }
    t_end               = (uint32_t)xTaskGetTickCount();
    ticks               = (t_end >= t_start) ? (t_end - t_start) : (0xFFFFFFFFu - t_start + t_end + 1u);
    write_with_erase_ms = ticks * portTICK_PERIOD_MS;
    if (write_with_erase_ms == 0u) { write_with_erase_ms = 1u; }
    write_with_erase_kbs = (float)total_bytes / 1024.0f * 1000.0f / (float)write_with_erase_ms;

    /* ---- Step 4: sequential read ---- */
    DBG("  Sequential read  %lu KB...\n", (unsigned long)test_size_kb);
    t_start = (uint32_t)xTaskGetTickCount();
    for (offset = 0u; offset < total_bytes; offset += chunk) {
        FlashDev_Read(dev, test_addr + offset, test_read_buffer, (uint32_t)chunk);
    }
    t_end    = (uint32_t)xTaskGetTickCount();
    ticks    = (t_end >= t_start) ? (t_end - t_start) : (0xFFFFFFFFu - t_start + t_end + 1u);
    read_ms  = ticks * portTICK_PERIOD_MS;
    if (read_ms == 0u) { read_ms = 1u; }
    read_kbs = (float)total_bytes / 1024.0f * 1000.0f / (float)read_ms;

    if (result) {
        result->test_passed              = true;
        result->pure_write_speed_kbs     = pure_write_kbs;
        result->pure_write_time_ms       = pure_write_ms;
        result->write_with_erase_speed_kbs = write_with_erase_kbs;
        result->write_with_erase_time_ms   = write_with_erase_ms;
        result->seq_read_speed_kbs       = read_kbs;
        result->read_time_ms             = read_ms;
        result->test_size_bytes          = total_bytes;
    }

    DBG("\n--- Speed Test Result ---\n");
    DBG("  Test size        : %lu KB\n", (unsigned long)test_size_kb);
    DBG("  Write (no erase) : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)pure_write_ms,       
        (unsigned long)pure_write_kbs, (unsigned long)((pure_write_kbs - (float)(unsigned long)pure_write_kbs) * 10.0f));
    DBG("  Write (w/ erase) : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)write_with_erase_ms, 
        (unsigned long)write_with_erase_kbs, (unsigned long)((write_with_erase_kbs - (float)(unsigned long)write_with_erase_kbs) * 10.0f));
    DBG("  Read             : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)read_ms,              
        (unsigned long)read_kbs, (unsigned long)((read_kbs - (float)(unsigned long)read_kbs) * 10.0f));
    DBG("-------------------------\n");
}

#endif /* NOR_FLASH_TEST */

/*===========================================================================
 * NAND Flash 测试
 *===========================================================================*/

#ifdef NAND_FLASH_TEST

/* 测试缓冲区：静态分配，避免占用任务�?*/
static uint8_t g_nand_wr_buf[W25N02_PAGE_SIZE];
static uint8_t g_nand_rd_buf[W25N02_PAGE_SIZE];

/**
 * @brief �?start_block 开始查找第一个好�? * @return 好块号，0xFFFF 表示全部为坏�? */
static uint16_t nand_find_good_block(FlashDevice_t *dev, uint16_t start_block)
{
    uint16_t blk;
    for (blk = start_block; blk < W25N02_BLOCK_COUNT; blk++) {
        if (!W25N02_IsBadBlock(dev, blk)) {
            return blk;
        }
    }
    return 0xFFFFu;
}

/*---------------------------------------------------------------------------
 * NandFlash_Test - 完整功能验证
 *---------------------------------------------------------------------------*/
void NandFlash_Test(void)
{
    FlashDevice_t *dev;
    uint16_t       test_blk;
    uint32_t       page_addr;
    uint16_t       i;
    bool           page_ok;

    DBG("\n**************************************************\n");
    DBG("*          NAND Flash Functional Test           *\n");
    DBG("**************************************************\n");
    spi_bus_gpio_diag();

    /* 获取 NAND 设备 */
    dev = FlashBus_GetDeviceByName("nand0");
    if (!dev || !dev->initialized) {
        DBG("[NAND] Device 'nand0' not available or not initialized\n");
        return;
    }

    /* 每次测试前复位设备，清除多次 BBM 扫描后的状态残留，
     * 并重新解除写保护（兼�?Zbit 等需�?WREN 的第三方芯片）�?*/
    W25N02_ResetDevice(dev);

    /* 1. JEDEC ID 检�?*/
    DBG("\n--- [1] JEDEC ID ---\n");
    dev->ops->read_id(dev);
    DBG("  Mfg=0x%02X  Type=0x%02X  Dev=0x%02X\n",
        dev->info.mfg_id, dev->info.mem_type, dev->info.dev_id);
    if (dev->info.mfg_id == W25N02_MFG_WINBOND) {
        DBG("[OK] Winbond manufacturer ID matched\n");
    } else {
        DBG("[WARN] Unexpected manufacturer: 0x%02X\n", dev->info.mfg_id);
    }

    /* 2. 坏块扫描 */
    DBG("\n--- [2] Bad Block Scan ---\n");
    if (W25N02_ScanBBT(dev) == FLASH_OK) {
        const W25N02_BBM_t *bbm = NULL;
        W25N02_GetBBM(dev, &bbm);
        if (bbm) {
            DBG("[OK] Scan done. Bad=%u / Good=%u / Total=%u\n",
                bbm->bad_count,
                (uint16_t)(W25N02_BLOCK_COUNT - bbm->bad_count),
                W25N02_BLOCK_COUNT);
        }
    }

    /* 3. 单页�?读验�?*/
    DBG("\n--- [3] Single Page Write/Read Verify ---\n");
    test_blk = nand_find_good_block(dev, 10u); /* 跳过�?10 �?*/
    if (test_blk == 0xFFFFu) {
        DBG("[FAIL] No good block available\n");
        return;
    }
    page_addr = (uint32_t)test_blk * W25N02_PAGES_PER_BLOCK;

    /* 准备测试数据 */
    for (i = 0; i < W25N02_PAGE_SIZE; i++) {
        g_nand_wr_buf[i] = (uint8_t)(0x5A ^ (i & 0xFFu));
    }

    /* 擦块 */
    if (W25N02_EraseBlock(dev, test_blk) != FLASH_OK) {
        DBG("[FAIL] Block %u erase failed\n", test_blk);
        return;
    }

    /* 写页 */
    if (W25N02_ProgramPage(dev, page_addr, 0,
                           g_nand_wr_buf, W25N02_PAGE_SIZE) != FLASH_OK) {
        DBG("[FAIL] Page 0x%04lX program failed\n", (unsigned long)page_addr);
        return;
    }

    /* Diagnostic: read first 8 bytes to check write+read path */
    {
        uint8_t diag[8];
        memset(diag, 0xEE, 8);
        if (W25N02_ReadPage(dev, page_addr, 0, diag, 8) == FLASH_OK) {
            DBG("[DIAG] First 8 bytes after write: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                diag[0], diag[1], diag[2], diag[3], diag[4], diag[5], diag[6], diag[7]);
            DBG("[DIAG] Expected:                  %02X %02X %02X %02X %02X %02X %02X %02X\n",
                g_nand_wr_buf[0], g_nand_wr_buf[1], g_nand_wr_buf[2], g_nand_wr_buf[3],
                g_nand_wr_buf[4], g_nand_wr_buf[5], g_nand_wr_buf[6], g_nand_wr_buf[7]);
        } else {
            DBG("[DIAG] 8-byte read failed\n");
        }
    }

    /* 读页 */
    memset(g_nand_rd_buf, 0, W25N02_PAGE_SIZE);
    if (W25N02_ReadPage(dev, page_addr, 0,
                        g_nand_rd_buf, W25N02_PAGE_SIZE) != FLASH_OK) {
        DBG("[FAIL] Page 0x%04lX read failed\n", (unsigned long)page_addr);
        return;
    }

    /* 逐字节校�?*/
    page_ok = true;
    for (i = 0; i < W25N02_PAGE_SIZE; i++) {
        if (g_nand_wr_buf[i] != g_nand_rd_buf[i]) {
            DBG("[FAIL] Verify error @ byte %u: wr=0x%02X rd=0x%02X\n",
                i, g_nand_wr_buf[i], g_nand_rd_buf[i]);
            page_ok = false;
            break;
        }
    }
    if (page_ok) {
        DBG("[OK] Page 0x%04lX (block %u) write/read verified (%u bytes)\n",
            (unsigned long)page_addr, test_blk, W25N02_PAGE_SIZE);
    }

    /* 擦除清理 */
    W25N02_EraseBlock(dev, test_blk);

    DBG("\n========================================\n");
    DBG("  NAND Functional Test Complete\n");
    DBG("========================================\n");
}

/*---------------------------------------------------------------------------
 * NandFlash_SpeedTest - 顺序读写速度测试
 *---------------------------------------------------------------------------*/
void NandFlash_SpeedTest(uint8_t test_blocks, NandTestResult_t *result)
{
    FlashDevice_t *dev;
    uint16_t       good_blocks[16]; /* 最�?16 �?*/
    uint8_t        found;
    uint8_t        blk_idx;
    uint16_t       page;
    uint16_t       i;
    uint16_t       scan_blk;
    uint32_t       t_start, t_end;
    uint32_t       write_ticks, read_ticks;
    uint32_t       write_ms, read_ms;
    uint32_t       total_bytes;
    float          write_kbs, read_kbs;

    if (test_blocks == 0u) {
        test_blocks = 4u;
    }
    if (test_blocks > 16u) {
        test_blocks = 16u;
    }

    DBG("\n=== NAND Speed Test (%u blocks, %u KB each) ===\n",
        test_blocks, W25N02_BLOCK_SIZE / 1024u);

    dev = FlashBus_GetDeviceByName("nand0");
    if (!dev || !dev->initialized) {
        DBG("[NAND Speed] Device not available\n");
        if (result) { result->test_passed = false; }
        return;
    }

    /* 准备写入数据模式 */
    for (i = 0; i < W25N02_PAGE_SIZE; i++) {
        g_nand_wr_buf[i] = (uint8_t)(0xA5u ^ i);
    }

    /* �?test_blocks 个好�?*/
    found    = 0u;
    scan_blk = 10u; /* 跳过�?10 块，避免触碰保留�?*/
    while (found < test_blocks) {
        scan_blk = nand_find_good_block(dev, scan_blk);
        if (scan_blk == 0xFFFFu) { break; }
        good_blocks[found] = scan_blk;
        found++;
        scan_blk++;
    }

    if (found == 0u) {
        DBG("[NAND Speed] No good blocks found\n");
        if (result) { result->test_passed = false; }
        return;
    }
    if (found < test_blocks) {
        DBG("[NAND Speed] Only %u good blocks available, test with %u\n",
            found, found);
        test_blocks = found;
    }

    total_bytes = (uint32_t)test_blocks * W25N02_BLOCK_SIZE;

    /* 预擦所有测试块 */
    DBG("  Erasing %u blocks...\n", test_blocks);
    for (blk_idx = 0u; blk_idx < test_blocks; blk_idx++) {
        W25N02_EraseBlock(dev, good_blocks[blk_idx]);
    }

    /* ---- 顺序写速度 ---- */
    DBG("  Sequential write %lu KB...\n", (unsigned long)(total_bytes / 1024u));
    t_start = (uint32_t)xTaskGetTickCount();

    for (blk_idx = 0u; blk_idx < test_blocks; blk_idx++) {
        uint32_t base_page = (uint32_t)good_blocks[blk_idx] * W25N02_PAGES_PER_BLOCK;
        for (page = 0u; page < W25N02_PAGES_PER_BLOCK; page++) {
            W25N02_ProgramPage(dev, base_page + page, 0u,
                               g_nand_wr_buf, W25N02_PAGE_SIZE);
        }
    }

    t_end       = (uint32_t)xTaskGetTickCount();
    write_ticks = (t_end >= t_start) ? (t_end - t_start)
                                     : (0xFFFFFFFFu - t_start + t_end + 1u);
    write_ms    = write_ticks * portTICK_PERIOD_MS;
    if (write_ms == 0u) { write_ms = 1u; }
    write_kbs   = (float)total_bytes / 1024.0f * 1000.0f / (float)write_ms;

    /* ---- 顺序读速度 ---- */
    DBG("  Sequential read  %lu KB...\n", (unsigned long)(total_bytes / 1024u));
    t_start = (uint32_t)xTaskGetTickCount();

    for (blk_idx = 0u; blk_idx < test_blocks; blk_idx++) {
        uint32_t base_page = (uint32_t)good_blocks[blk_idx] * W25N02_PAGES_PER_BLOCK;
        for (page = 0u; page < W25N02_PAGES_PER_BLOCK; page++) {
            W25N02_ReadPage(dev, base_page + page, 0u,
                            g_nand_rd_buf, W25N02_PAGE_SIZE);
        }
    }

    t_end      = (uint32_t)xTaskGetTickCount();
    read_ticks = (t_end >= t_start) ? (t_end - t_start)
                                    : (0xFFFFFFFFu - t_start + t_end + 1u);
    read_ms    = read_ticks * portTICK_PERIOD_MS;
    if (read_ms == 0u) { read_ms = 1u; }
    read_kbs   = (float)total_bytes / 1024.0f * 1000.0f / (float)read_ms;

    /* 清理：再次擦�?*/
    for (blk_idx = 0u; blk_idx < test_blocks; blk_idx++) {
        W25N02_EraseBlock(dev, good_blocks[blk_idx]);
    }

    /* 统计结果 */
    if (result) {
        result->test_passed         = true;
        result->seq_write_speed_kbs = write_kbs;
        result->seq_read_speed_kbs  = read_kbs;
        result->write_time_ms       = write_ms;
        result->read_time_ms        = read_ms;
        result->test_size_bytes     = total_bytes;
        {
            const W25N02_BBM_t *bbm = NULL;
            W25N02_GetBBM(dev, &bbm);
            result->bad_block_count = bbm ? bbm->bad_count : 0u;
            DBG("[DEBUG] In NandFlash_SpeedTest: bbm=%p, bad_count from bbm=%u, result->bad_block_count=%u\n",
                (void*)bbm, bbm ? bbm->bad_count : 0u, result->bad_block_count);
        }
    }

    DBG("\n--- Speed Test Result ---\n");
    DBG("  Test size : %lu KB (%u blocks x %u KB)\n",
        (unsigned long)(total_bytes / 1024u),
        test_blocks, W25N02_BLOCK_SIZE / 1024u);
    DBG("  Write     : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)write_ms, 
        (unsigned long)write_kbs, (unsigned long)((write_kbs - (float)(unsigned long)write_kbs) * 10.0f));
    DBG("  Read      : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)read_ms,  
        (unsigned long)read_kbs, (unsigned long)((read_kbs - (float)(unsigned long)read_kbs) * 10.0f));
    DBG("-------------------------\n");
}

/*---------------------------------------------------------------------------
 * NandFlash_BBMTest - 坏块管理专项测试
 *---------------------------------------------------------------------------*/
void NandFlash_BBMTest(void)
{
    FlashDevice_t      *dev;
    const W25N02_BBM_t *bbm = NULL;
    uint16_t            blk;

    DBG("\n=== NAND Bad Block Management Test ===\n");
    spi_bus_gpio_diag();

    dev = FlashBus_GetDeviceByName("nand0");
    if (!dev || !dev->initialized) {
        DBG("[BBM] Device 'nand0' not available\n");
        return;
    }

    /* 扫描前复位确保芯片就绪，同时重新解除写保�?*/
    W25N02_ResetDevice(dev);

    /* 全片扫描坏块 */
    if (W25N02_ScanBBT(dev) != FLASH_OK) {
        DBG("[BBM] ScanBBT failed\n");
        return;
    }

    if (W25N02_GetBBM(dev, &bbm) != FLASH_OK || !bbm) {
        DBG("[BBM] GetBBM failed\n");
        return;
    }

    if (bbm->bad_count == 0u) {
        DBG("  No bad blocks found (all %u blocks are good)\n", W25N02_BLOCK_COUNT);
    } else {
        DBG("  Bad block list:\n");
        for (blk = 0u; blk < W25N02_BLOCK_COUNT; blk++) {
            if (W25N02_IsBadBlock(dev, blk)) {
                DBG("    Block %4u  (0x%03X)  addr=0x%06lX\n",
                    blk, blk, (unsigned long)((uint32_t)blk * W25N02_BLOCK_SIZE));
            }
        }
    }

    DBG("\n  Summary:\n");
    DBG("    Total blocks : %u\n", W25N02_BLOCK_COUNT);
    DBG("    Bad  blocks  : %u\n", bbm->bad_count);
    DBG("    Good blocks  : %u\n", (uint16_t)(W25N02_BLOCK_COUNT - bbm->bad_count));
    DBG("    Bad rate     : %.2f%%\n",
        (float)bbm->bad_count * 100.0f / (float)W25N02_BLOCK_COUNT);
    DBG("======================================\n");
}

#endif /* NAND_FLASH_TEST */

/*===========================================================================
 * PSRAM 测试实现
 *===========================================================================*/

#ifdef PSRAM_TEST

#include "psram_esp64h.h"

/* 测试缓冲区：静态分�?*/
static uint8_t g_psram_wr_buf[4096];
static uint8_t g_psram_rd_buf[4096];

/*---------------------------------------------------------------------------
 * PsramFlash_Test - 完整功能验证
 *---------------------------------------------------------------------------*/
void PsramFlash_Test(void)
{
    FlashDevice_t *dev;
    uint8_t        mfg_id, kgd;
    uint32_t       test_addr = 0x1000; /* 测试地址 */
    uint16_t       i;
    bool           data_ok;

    DBG("\n**************************************************\n");
    DBG("*         PSRAM Functional Test                  *\n");
    DBG("**************************************************\n");
    spi_bus_gpio_diag();

    /* 获取 PSRAM 设备 */
    dev = FlashBus_GetDeviceByName("psram0");
    if (!dev || !dev->initialized) {
        DBG("[PSRAM] Device 'psram0' not available or not initialized\n");
        return;
    }

    /* 1. ID 检�?*/
    DBG("\n--- [1] Electronic ID ---\n");
    if (PSRAM64H_ReadID(dev, &mfg_id, &kgd) == FLASH_OK) {
        DBG("  Mfg=0x%02X  KGD=0x%02X\n", mfg_id, kgd);
        if (mfg_id == PSRAM64H_KNOWN_MFG_ID) {
            DBG("[OK] Manufacturer ID matched\n");
        } else if (mfg_id == 0x00u && kgd == 0x00u) {
            DBG("[INFO] ReadID all zero - chip may not support 0x9F, continue R/W test\n");
        } else {
            DBG("[WARN] Unexpected manufacturer: 0x%02X\n", mfg_id);
        }
    } else {
        DBG("[WARN] Read ID command failed, continue R/W test\n");
    }

    /* 2. 容量信息 */
    DBG("\n--- [2] Device Info ---\n");
    DBG("  Total Size: %lu MB\n", PSRAM64H_TOTAL_SIZE / (1024u * 1024u));
    DBG("  Page Bound: %u bytes\n", PSRAM64H_PAGE_SIZE);

    /* 3. 单字节读写测�?*/
    DBG("\n--- [3] Single Byte Test ---\n");
    test_addr = 0x1000u;

    /* 先读取原始�?(诊断�? */
    g_psram_rd_buf[0] = 0xEE;
    PSRAM64H_DirectRead(dev, test_addr, g_psram_rd_buf, 1u);
    DBG("  Before write: addr=0x%04lX  val=0x%02X\n",
        (unsigned long)test_addr, g_psram_rd_buf[0]);

    /* �?0xA5 */
    {
        uint8_t wr_byte = 0xA5u;
        if (PSRAM64H_DirectWrite(dev, test_addr, &wr_byte, 1u) != FLASH_OK) {
            DBG("[FAIL] Single byte write failed\n");
            return;
        }
    }

    g_psram_rd_buf[0] = 0x00;
    if (PSRAM64H_DirectRead(dev, test_addr, g_psram_rd_buf, 1u) != FLASH_OK) {
        DBG("[FAIL] Single byte read failed\n");
        return;
    }
    DBG("  After write 0xA5: read=0x%02X\n", g_psram_rd_buf[0]);

    if (g_psram_rd_buf[0] != 0xA5u) {
        /* 二次确认: �?0x5A 再读 */
        uint8_t wr2 = 0x5Au;
        uint8_t rd2 = 0x00;
        PSRAM64H_DirectWrite(dev, test_addr, &wr2, 1u);
        PSRAM64H_DirectRead(dev, test_addr, &rd2, 1u);
        DBG("  2nd write 0x5A: read=0x%02X\n", rd2);
        if (rd2 != 0x5Au) {
            DBG("[FAIL] Single byte R/W not working\n");
        } else {
            DBG("[OK] 2nd attempt passed (1st may be residual data)\n");
        }
        /* �?return，继续后续测�?*/
    } else {
        DBG("[OK] Single byte write/read verified (0xA5)\n");
    }

    /* 4. 8字节读写测试 (DMA 与逐字节边界验证) */
    DBG("\n--- [4] 8-Byte Write/Read Test ---\n");
    test_addr = 0x3000u;
    {
        uint8_t wr8[8];
        uint8_t rd8[8];
        uint8_t j;
        for (j = 0; j < 8u; j++) { wr8[j] = (uint8_t)(0xA0u + j); }
        memset(rd8, 0, 8u);
        PSRAM64H_DirectWrite(dev, test_addr, wr8, 8u);
        PSRAM64H_DirectRead(dev, test_addr, rd8, 8u);
        DBG("  Wrote: ");
        for (j = 0; j < 8u; j++) { DBG("%02X ", wr8[j]); }
        DBG("\n  Read:  ");
        for (j = 0; j < 8u; j++) { DBG("%02X ", rd8[j]); }
        DBG("\n");
        if (memcmp(wr8, rd8, 8u) == 0) {
            DBG("[OK] 8-byte R/W verified\n");
        } else {
            DBG("[FAIL] 8-byte R/W mismatch\n");
        }
    }

    /* 5. 多字节读写测试 (4KB) */
    DBG("\n--- [5] Multi-Byte Test (4KB) ---\n");
    test_addr = 0x2000u;

    /* 准备测试数据 */
    for (i = 0u; i < 4096u; i++) {
        g_psram_wr_buf[i] = (uint8_t)(0x5Au ^ (i & 0xFFu));
    }

    /* 写入 */
    if (PSRAM64H_DirectWrite(dev, test_addr, g_psram_wr_buf, 4096u) != FLASH_OK) {
        DBG("[FAIL] 4KB write failed\n");
        return;
    }

    /* 读取 */
    memset(g_psram_rd_buf, 0, 4096u);
    if (PSRAM64H_DirectRead(dev, test_addr, g_psram_rd_buf, 4096u) != FLASH_OK) {
        DBG("[FAIL] 4KB read failed\n");
        return;
    }

    /* 校验 */
    data_ok = true;
    for (i = 0u; i < 4096u; i++) {
        if (g_psram_wr_buf[i] != g_psram_rd_buf[i]) {
            DBG("[FAIL] Data mismatch @ byte %u: wr=0x%02X rd=0x%02X\n",
                i, g_psram_wr_buf[i], g_psram_rd_buf[i]);
            data_ok = false;
            break;
        }
    }
    if (data_ok) {
        DBG("[OK] 4KB write/read verified\n");
    }

    /* 5. 跨页边界测试 */
    DBG("\n--- [5] Cross-Page Boundary Test ---\n");
    test_addr = PSRAM64H_PAGE_SIZE - 512u; /* �?1KB 页边�?*/

    for (i = 0u; i < 1024u; i++) {
        g_psram_wr_buf[i] = (uint8_t)(0x3Cu ^ i);
    }

    if (PSRAM64H_DirectWrite(dev, test_addr, g_psram_wr_buf, 1024u) != FLASH_OK) {
        DBG("[FAIL] Cross-page write failed\n");
        return;
    }

    memset(g_psram_rd_buf, 0, 1024u);
    if (PSRAM64H_DirectRead(dev, test_addr, g_psram_rd_buf, 1024u) != FLASH_OK) {
        DBG("[FAIL] Cross-page read failed\n");
        return;
    }

    data_ok = true;
    for (i = 0u; i < 1024u; i++) {
        if (g_psram_wr_buf[i] != g_psram_rd_buf[i]) {
            DBG("[FAIL] Cross-page mismatch @ %u\n", i);
            data_ok = false;
            break;
        }
    }
    if (data_ok) {
        DBG("[OK] Cross-page boundary test passed\n");
    }

    DBG("\n========================================\n");
    DBG("  PSRAM Functional Test Complete\n");
    DBG("========================================\n");
}

/*---------------------------------------------------------------------------
 * PsramFlash_SpeedTest - 顺序读写速度测试
 *---------------------------------------------------------------------------*/
void PsramFlash_SpeedTest(uint32_t test_size_kb, PsramTestResult_t *result)
{
    FlashDevice_t *dev;
    uint32_t       test_addr = 0x10000u; /* 测试起始地址 64KB */
    uint32_t       chunk_size = 2048u;   /* DMA 单次最�?2048 字节 (12-bit block reg) */
    uint32_t       total_bytes;
    uint32_t       offset;
    uint32_t       t_start, t_end;
    uint32_t       write_ticks, read_ticks;
    uint32_t       write_ms, read_ms;
    float          write_kbs, read_kbs;
    uint16_t       i;

    if (test_size_kb == 0u) {
        test_size_kb = 512u; /* 默认 512KB */
    }
    if (test_size_kb > 7 * 1024u) { /* 限制�?7MB，避免溢�?*/
        test_size_kb = 7 * 1024u;
    }

    total_bytes = test_size_kb * 1024u;

    DBG("\n=== PSRAM Speed Test (%lu KB) ===\n", (unsigned long)test_size_kb);

    dev = FlashBus_GetDeviceByName("psram0");
    if (!dev || !dev->initialized) {
        DBG("[PSRAM Speed] Device not available\n");
        if (result) { result->test_passed = false; }
        return;
    }

    /* 准备写入数据模式 */
    for (i = 0u; i < chunk_size; i++) {
        g_psram_wr_buf[i] = (uint8_t)(0xA5u ^ (i & 0xFFu));
    }

    /* ---- 顺序写速度 ---- */
    DBG("  Sequential write %lu KB...\n", (unsigned long)test_size_kb);
    t_start = (uint32_t)xTaskGetTickCount();

    for (offset = 0u; offset < total_bytes; offset += chunk_size) {
        PSRAM64H_DirectWrite(dev, test_addr + offset, g_psram_wr_buf, chunk_size);
    }

    t_end       = (uint32_t)xTaskGetTickCount();
    write_ticks = (t_end >= t_start) ? (t_end - t_start)
                                     : (0xFFFFFFFFu - t_start + t_end + 1u);
    write_ms    = write_ticks * portTICK_PERIOD_MS;
    if (write_ms == 0u) { write_ms = 1u; }
    write_kbs   = (float)total_bytes / 1024.0f * 1000.0f / (float)write_ms;

    /* ---- 顺序读速度 ---- */
    DBG("  Sequential read  %lu KB...\n", (unsigned long)test_size_kb);
    t_start = (uint32_t)xTaskGetTickCount();

    for (offset = 0u; offset < total_bytes; offset += chunk_size) {
        PSRAM64H_DirectRead(dev, test_addr + offset, g_psram_rd_buf, chunk_size);
    }

    t_end      = (uint32_t)xTaskGetTickCount();
    read_ticks = (t_end >= t_start) ? (t_end - t_start)
                                    : (0xFFFFFFFFu - t_start + t_end + 1u);
    read_ms    = read_ticks * portTICK_PERIOD_MS;
    if (read_ms == 0u) { read_ms = 1u; }
    read_kbs   = (float)total_bytes / 1024.0f * 1000.0f / (float)read_ms;

    /* 统计结果 */
    if (result) {
        result->test_passed         = true;
        result->seq_write_speed_kbs = write_kbs;
        result->seq_read_speed_kbs  = read_kbs;
        result->write_time_ms       = write_ms;
        result->read_time_ms        = read_ms;
        result->test_size_bytes     = total_bytes;
    }

    DBG("\n--- Speed Test Result ---\n");
    DBG("  Test size : %lu KB\n", (unsigned long)test_size_kb);
    DBG("  Write     : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)write_ms, 
        (unsigned long)write_kbs, (unsigned long)((write_kbs - (float)(unsigned long)write_kbs) * 10.0f));
    DBG("  Read      : %lu ms  ->  %lu.%lu KB/s\n", (unsigned long)read_ms,  
        (unsigned long)read_kbs, (unsigned long)((read_kbs - (float)(unsigned long)read_kbs) * 10.0f));
    DBG("-------------------------\n");
}

#endif /* PSRAM_TEST */

/*===========================================================================
 * SD Card 测试
 *===========================================================================*/
#ifdef SDCARD_TEST

#include "sd_card_driver.h"
#include "hal_sdio.h"

/* 测试参数 */
#define SDCARD_TEST_START_BLOCK    1000
#define SDCARD_DEFAULT_TEST_BLOCKS 1024

/**
 * @brief SD Card 完整功能测试
 */
void SDCardFlash_Test(void)
{
    FlashDevice_t *dev = FlashDevices_GetSDCardFlash();
    uint32_t capacity, block_size;
    uint8_t test_buf[SD_CARD_BLOCK_SIZE];
    uint8_t verify_buf[SD_CARD_BLOCK_SIZE];
    uint32_t test_addr;
    uint16_t i;
    uint8_t *multi_buf;
    uint8_t *multi_verify;
    
    DBG("\n");
    DBG("========================================\n");
    DBG("  SD Card Functional Test\n");
    DBG("========================================\n\n");
    spi_bus_gpio_diag();
    
    if (!dev) {
        DBG("[FAIL] SD Card device not found\n");
        return;
    }
    
    if (!dev->initialized) {
        DBG("[WARN] SD Card not initialized, initializing...\n");
        if (FlashDev_Init(dev) != FLASH_OK) {
            DBG("[FAIL] SD Card init failed\n");
            return;
        }
    }
    
    /* 获取设备信息 */
    DBG("*** [1] Card Info ***\n");
    capacity = dev->info.total_size;
    DBG("  Capacity: %lu MB\n", capacity / (1024 * 1024));
    
    block_size = dev->info.block_size;
    DBG("  Block Size: %lu bytes\n", block_size);
    DBG("\n");
    
    /* 单块写入测试 */
    DBG("*** [2] Single Block Write/Read Test ***\n");
    test_addr = SDCARD_TEST_START_BLOCK * SD_CARD_BLOCK_SIZE;
    
    /* 填充测试数据 */
    for (i = 0; i < SD_CARD_BLOCK_SIZE; i++) {
        test_buf[i] = (uint8_t)(i & 0xFF);
    }
    
    /* 写入 */
    if (FlashDev_Write(dev, test_addr, test_buf, SD_CARD_BLOCK_SIZE) != FLASH_OK) {
        DBG("[FAIL] Single block write failed\n");
        return;
    }
    
    /* 读回验证 */
    memset(verify_buf, 0, SD_CARD_BLOCK_SIZE);
    if (FlashDev_Read(dev, test_addr, verify_buf, SD_CARD_BLOCK_SIZE) != FLASH_OK) {
        DBG("[FAIL] Single block read failed\n");
        return;
    }
    
    if (memcmp(test_buf, verify_buf, SD_CARD_BLOCK_SIZE) != 0) {
        /* 打印�?6字节详细对比 */
        DBG("[FAIL] Single block verify failed. First 16 bytes:\n");
        DBG("  Expected: ");
        for (i = 0; i < 16; i++) { DBG("%02X ", test_buf[i]); }
        DBG("\n  Got:      ");
        for (i = 0; i < 16; i++) { DBG("%02X ", verify_buf[i]); }
        DBG("\n");
        /* �?return，继续多块测�?*/
    } else {
        DBG("[OK] Single block write/read verified\n\n");
    }
    
    /* 多块写入测试 */
    DBG("*** [3] Multi-Block Test (4KB = 8 blocks) ***\n");
    multi_buf = (uint8_t *)pvPortMalloc(4096);
    multi_verify = (uint8_t *)pvPortMalloc(4096);
    
    if (!multi_buf || !multi_verify) {
        DBG("[FAIL] Memory allocation failed\n");
        if (multi_buf) vPortFree(multi_buf);
        if (multi_verify) vPortFree(multi_verify);
        return;
    }
    
    /* 填充测试数据 */
    for (i = 0; i < 4096; i++) {
        multi_buf[i] = (uint8_t)((i * 7 + 13) & 0xFF);
    }
    
    test_addr = (SDCARD_TEST_START_BLOCK + 10) * SD_CARD_BLOCK_SIZE;
    
    /* 写入 */
    if (FlashDev_Write(dev, test_addr, multi_buf, 4096) != FLASH_OK) {
        DBG("[FAIL] Multi-block write failed\n");
        vPortFree(multi_buf);
        vPortFree(multi_verify);
        return;
    }
    
    /* 读回验证 */
    memset(multi_verify, 0, 4096);
    if (FlashDev_Read(dev, test_addr, multi_verify, 4096) != FLASH_OK) {
        DBG("[FAIL] Multi-block read failed\n");
        vPortFree(multi_buf);
        vPortFree(multi_verify);
        return;
    }
    
    if (memcmp(multi_buf, multi_verify, 4096) != 0) {
        DBG("[FAIL] Multi-block verify failed\n");
        vPortFree(multi_buf);
        vPortFree(multi_verify);
        return;
    }
    
    vPortFree(multi_buf);
    vPortFree(multi_verify);
    DBG("[OK] Multi-block write/read verified\n\n");
    
    DBG("========================================\n");
    DBG("  SD Card Functional Test Complete\n");
    DBG("========================================\n\n");
}

/**
 * @brief SD Card 顺序读写速度测试 */
void SDCardFlash_SpeedTest(uint32_t test_blocks, SDCardTestResult_t *result)
{
    /* 使用固定小缓冲区 (4KB = 8�? 分批读写，避免单次分�?512KB+ 失败�?     * HAL 现使用多�?CMD25/CMD18�?�?次，预期速度�?1000~3000 KB/s�?*/
    #define SD_SPEED_CHUNK_BLOCKS  8u
    #define SD_SPEED_CHUNK_BYTES   (SD_SPEED_CHUNK_BLOCKS * SD_CARD_BLOCK_SIZE)

    FlashDevice_t *dev = FlashDevices_GetSDCardFlash();
    uint32_t test_size;
    uint8_t  chunk_buf[SD_SPEED_CHUNK_BYTES];  /* 4096 bytes on stack (SRAM, DMA accessible) */
    uint32_t test_addr;
    uint32_t start_tick, end_tick;
    uint32_t write_time_ms, read_time_ms;
    float write_speed_kbs, read_speed_kbs;
    uint32_t offset;
    uint32_t blks_this;
    uint32_t k;

    if (test_blocks == 0) {
        test_blocks = SDCARD_DEFAULT_TEST_BLOCKS;
    }

    test_size = test_blocks * SD_CARD_BLOCK_SIZE;

    DBG("\n");
    DBG("=== SD Card Speed Test (%lu KB) ===\n", test_size / 1024);

    if (!dev || !dev->initialized) {
        DBG("[FAIL] SD Card not available\n");
        if (result) {
            memset(result, 0, sizeof(SDCardTestResult_t));
            result->test_passed = false;
        }
        return;
    }

    test_addr = SDCARD_TEST_START_BLOCK * SD_CARD_BLOCK_SIZE;

    /* ---- 顺序写入速度测试 (分块, 每次 4KB) ---- */
    DBG("  Sequential write %lu KB...\n", test_size / 1024);
    start_tick = xTaskGetTickCount();

    for (offset = 0; offset < test_blocks; offset += SD_SPEED_CHUNK_BLOCKS) {
        blks_this = test_blocks - offset;
        if (blks_this > SD_SPEED_CHUNK_BLOCKS) {
            blks_this = SD_SPEED_CHUNK_BLOCKS;
        }
        /* 生成当前块的测试数据 */
        for (k = 0; k < blks_this * SD_CARD_BLOCK_SIZE; k++) {
            chunk_buf[k] = (uint8_t)(((offset * SD_CARD_BLOCK_SIZE + k) * 3 + 7) & 0xFF);
        }
        if (FlashDev_Write(dev, test_addr + offset * SD_CARD_BLOCK_SIZE,
                           chunk_buf, blks_this * SD_CARD_BLOCK_SIZE) != FLASH_OK) {
            DBG("[FAIL] Write failed at block offset %lu\n", (unsigned long)offset);
            if (result) {
                memset(result, 0, sizeof(SDCardTestResult_t));
                result->test_passed = false;
            }
            return;
        }
    }

    end_tick = xTaskGetTickCount();
    write_time_ms = (end_tick - start_tick) * portTICK_PERIOD_MS;
    if (write_time_ms == 0) write_time_ms = 1;
    write_speed_kbs = (float)(test_size / 1024) / ((float)write_time_ms / 1000.0f);

    /* ---- 顺序读取速度测试 (分块, 每次 4KB) ---- */
    DBG("  Sequential read  %lu KB...\n", test_size / 1024);
    start_tick = xTaskGetTickCount();

    for (offset = 0; offset < test_blocks; offset += SD_SPEED_CHUNK_BLOCKS) {
        blks_this = test_blocks - offset;
        if (blks_this > SD_SPEED_CHUNK_BLOCKS) {
            blks_this = SD_SPEED_CHUNK_BLOCKS;
        }
        if (FlashDev_Read(dev, test_addr + offset * SD_CARD_BLOCK_SIZE,
                          chunk_buf, blks_this * SD_CARD_BLOCK_SIZE) != FLASH_OK) {
            DBG("[FAIL] Read failed at block offset %lu\n", (unsigned long)offset);
            if (result) {
                memset(result, 0, sizeof(SDCardTestResult_t));
                result->test_passed = false;
            }
            return;
        }
    }

    end_tick = xTaskGetTickCount();
    read_time_ms = (end_tick - start_tick) * portTICK_PERIOD_MS;
    if (read_time_ms == 0) read_time_ms = 1;
    read_speed_kbs = (float)(test_size / 1024) / ((float)read_time_ms / 1000.0f);
    DBG("\n");
    DBG("--- Speed Test Result ---\n");
    DBG("  Test size : %lu KB\n", test_size / 1024);
    DBG("  Write     : %lu ms  ->  %lu.%lu KB/s\n", write_time_ms, 
        (unsigned long)write_speed_kbs, (unsigned long)((write_speed_kbs - (float)(unsigned long)write_speed_kbs) * 10.0f));
    DBG("  Read      : %lu ms  ->  %lu.%lu KB/s\n", read_time_ms, 
        (unsigned long)read_speed_kbs, (unsigned long)((read_speed_kbs - (float)(unsigned long)read_speed_kbs) * 10.0f));
    DBG("-------------------------\n\n");
    
    /* 填充结果 */
    if (result) {
        result->test_passed = true;
        result->seq_write_speed_kbs = write_speed_kbs;
        result->seq_read_speed_kbs = read_speed_kbs;
        result->write_time_ms = write_time_ms;
        result->read_time_ms = read_time_ms;
        result->test_size_bytes = test_size;
        
        result->card_capacity_mb = dev->info.total_size / (1024 * 1024);
    }
}

#endif /* SDCARD_TEST */
