/**
 *****************************************************************************
 * @file     flash_test.h
 * @brief    Flash 驱动框架测试头文件 (NOR + NAND)
 *****************************************************************************
 */

#ifndef __FLASH_TEST_H__
#define __FLASH_TEST_H__

#include <stdint.h>
#include <stdbool.h>
#include "banux_config.h"

/*===========================================================================
 * NOR Flash 测试
 *===========================================================================*/
#if FLASH_TEST_EN
#define NOR_FLASH_TEST
#endif

#ifdef NOR_FLASH_TEST
/**
 * @brief NOR Flash driver framework full functional test
 */
void FlashNewDriver_Test(void);

/**
 * @brief NOR Flash quick functional test (debug)
 */
void FlashNewDriver_QuickTest(void);

/**
 * @brief NOR Flash speed test result
 */
typedef struct {
    bool     test_passed;
    float    pure_write_speed_kbs;       /* write only, region pre-erased */
    float    write_with_erase_speed_kbs; /* erase+write back-to-back */
    float    seq_read_speed_kbs;
    uint32_t pure_write_time_ms;
    uint32_t write_with_erase_time_ms;
    uint32_t read_time_ms;
    uint32_t test_size_bytes;
} NorTestResult_t;

/**
 * @brief NOR Flash sequential read/write speed test
 * @param test_size_kb  Size in KB (0 = default 512 KB)
 * @param result        Output struct, may be NULL
 */
void NorFlash_SpeedTest(uint32_t test_size_kb, NorTestResult_t *result);
#endif /* NOR_FLASH_TEST */

/*===========================================================================
 * NAND Flash 测试
 *===========================================================================*/
#if FLASH_TEST_EN
#define NAND_FLASH_TEST
#endif

#ifdef NAND_FLASH_TEST

/**
 * @brief NAND Flash 完整功能测试结果
 */
typedef struct {
    bool     test_passed;           /* 整体测试是否通过 */
    float    seq_write_speed_kbs;   /* 顺序写速度 (KB/s) */
    float    seq_read_speed_kbs;    /* 顺序读速度 (KB/s) */
    uint32_t write_time_ms;         /* 速度测试写耗时 (ms) */
    uint32_t read_time_ms;          /* 速度测试读耗时 (ms) */
    uint16_t bad_block_count;       /* 坏块数量 */
    uint32_t test_size_bytes;       /* 本次速度测试数据量 */
} NandTestResult_t;

/**
 * @brief NAND Flash 完整功能测试 (读写验证 + 坏块管理)
 *
 * 测试内容:
 *   1. 设备初始化及 JEDEC ID 校验
 *   2. 单页编程/读取验证
 *   3. 跨页读写验证
 *   4. 块擦除验证
 *   5. 坏块扫描与报告
 */
void NandFlash_Test(void);

/**
 * @brief NAND Flash 顺序读写速度测试
 *
 * 测试方法:
 *   - 连续写入 test_blocks 个好块 (每块 64 页 x 2048 bytes = 128KB)
 *   - 记录写入耗时并计算 KB/s
 *   - 顺序读回相同区域
 *   - 记录读取耗时并计算 KB/s
 *
 * @param test_blocks  参与测试的块数，0 = 使用默认值 4
 * @param result       输出结果结构，可为 NULL
 */
void NandFlash_SpeedTest(uint8_t test_blocks, NandTestResult_t *result);

/**
 * @brief NAND Flash 坏块管理专项测试
 *        扫描所有块并打印坏块位置及统计信息
 */
void NandFlash_BBMTest(void);


#endif /* NAND_FLASH_TEST */

/*===========================================================================
 * PSRAM 测试
 *===========================================================================*/
#if FLASH_TEST_EN
#define PSRAM_TEST
#endif

#ifdef PSRAM_TEST

/**
 * @brief PSRAM 完整功能测试结果
 */
typedef struct {
    bool     test_passed;           /* 整体测试是否通过 */
    float    seq_write_speed_kbs;   /* 顺序写速度 (KB/s) */
    float    seq_read_speed_kbs;    /* 顺序读速度 (KB/s) */
    uint32_t write_time_ms;         /* 速度测试写耗时 (ms) */
    uint32_t read_time_ms;          /* 速度测试读耗时 (ms) */
    uint32_t test_size_bytes;       /* 本次速度测试数据量 */
} PsramTestResult_t;

/**
 * @brief PSRAM 完整功能测试 (读写验证)
 *
 * 测试内容:
 *   1. 设备初始化及 ID 校验
 *   2. 单字节/多字节读写验证
 *   3. 跨页边界读写验证
 *   4. 数据完整性验证
 */
void PsramFlash_Test(void);

/**
 * @brief PSRAM 顺序读写速度测试
 *
 * 测试方法:
 *   - 连续写入 test_size_kb 数据 (字节寻址)
 *   - 记录写入耗时并计算 KB/s
 *   - 顺序读回相同区域
 *   - 记录读取耗时并计算 KB/s
 *
 * @param test_size_kb  测试数据量 (KB)，0 = 使用默认值 512KB
 * @param result        输出结果结构，可为 NULL
 */
void PsramFlash_SpeedTest(uint32_t test_size_kb, PsramTestResult_t *result);


#endif /* PSRAM_TEST */

/*===========================================================================
 * SD Card 测试
 *===========================================================================*/
#if FLASH_TEST_EN
#define SDCARD_TEST
#endif

#ifdef SDCARD_TEST

/**
 * @brief SD Card 完整功能测试结果
 */
typedef struct {
    bool     test_passed;           /* 整体测试是否通过 */
    float    seq_write_speed_kbs;   /* 顺序写速度 (KB/s) */
    float    seq_read_speed_kbs;    /* 顺序读速度 (KB/s) */
    uint32_t write_time_ms;         /* 速度测试写耗时 (ms) */
    uint32_t read_time_ms;          /* 速度测试读耗时 (ms) */
    uint32_t test_size_bytes;       /* 本次速度测试数据量 */
    uint32_t card_capacity_mb;      /* SD卡容量 (MB) */
} SDCardTestResult_t;

/**
 * @brief SD Card 完整功能测试 (读写验证)
 *
 * 测试内容:
 *   1. 设备初始化及卡信息读取
 *   2. 单块/多块读写验证
 *   3. 数据完整性验证
 */
void SDCardFlash_Test(void);

/**
 * @brief SD Card 顺序读写速度测试
 *
 * 测试方法:
 *   - 连续写入 test_blocks 个块 (每块 512 字节)
 *   - 记录写入耗时并计算 KB/s
 *   - 顺序读回相同区域
 *   - 记录读取耗时并计算 KB/s
 *
 * @param test_blocks   测试块数，0 = 使用默认值 1024 (512KB)
 * @param result        输出结果结构，可为 NULL
 */
void SDCardFlash_SpeedTest(uint32_t test_blocks, SDCardTestResult_t *result);


#endif /* SDCARD_TEST */

#endif /* __FLASH_TEST_H__ */
