/**
 *****************************************************************************
 * @file     psram_esp64h.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    ESP-PSRAM64H SPI PSRAM 驱动
 *
 * 支持型号: ESP-PSRAM64H (64Mbit / 8MB)
 *
 * 规格:
 *   - 容量      : 64Mbit = 8MB
 *   - 页大小    : 1024 bytes (页边界写绕回)
 *   - 接口      : SPI / QPI
 *   - 最大时钟  : 84MHz (SPI), 144MHz (QPI)
 *   - 工作电压  : 1.8V
 *   - ID        : 0x0D 0x5D
 *   - 地址空间  : 24-bit (0x000000 ~ 0x7FFFFF)
 *
 * 特性:
 *   - 无需擦除，可直接写入任意地址
 *   - 支持突发读/写（burst read/write）
 *   - 无坏块概念 (RAM)
 *   - 低功耗 standby 模式
 *****************************************************************************
 */

#ifndef __PSRAM_ESP64H_H__
#define __PSRAM_ESP64H_H__

#include "flash_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * ESP-PSRAM64H 规格常量
 *===========================================================================*/

#define PSRAM64H_TOTAL_SIZE         (8u * 1024u * 1024u)  /* 8MB */
#define PSRAM64H_PAGE_SIZE          1024u                  /* 页边界: 1KB */
#define PSRAM64H_SECTOR_SIZE        PSRAM64H_PAGE_SIZE     /* 无扇区概念，映射到页 */
#define PSRAM64H_BLOCK_SIZE         (64u * 1024u)          /* 虚拟块: 64KB (兼容框架) */
#define PSRAM64H_BLOCK_COUNT        (PSRAM64H_TOTAL_SIZE / PSRAM64H_BLOCK_SIZE) /* 128 */

/*===========================================================================
 * ESP-PSRAM64H SPI 命令集
 *===========================================================================*/

#define PSRAM64H_CMD_READ           0x03    /* 慢速读 (最高 33MHz) */
#define PSRAM64H_CMD_FAST_READ      0x0B    /* 快速读 (最高 84MHz/144MHz) */
#define PSRAM64H_CMD_WRITE          0x02    /* 写入 */
#define PSRAM64H_CMD_QUAD_READ      0xEB    /* Quad I/O 快速读 */
#define PSRAM64H_CMD_QUAD_WRITE     0x38    /* Quad 写入 */
#define PSRAM64H_CMD_ENTER_QPI      0x35    /* 进入 QPI 模式 */
#define PSRAM64H_CMD_EXIT_QPI       0xF5    /* 退出 QPI 模式 */
#define PSRAM64H_CMD_RESET_ENABLE   0x66    /* 复位使能 */
#define PSRAM64H_CMD_RESET          0x99    /* 复位 */
#define PSRAM64H_CMD_READ_ID        0x9F    /* 读取 ID */
#define PSRAM64H_CMD_WRAP_TOGGLE    0xC0    /* burst wrap 设置 */

/*===========================================================================
 * 厂商 / 设备 ID
 *===========================================================================*/

#define PSRAM64H_KNOWN_MFG_ID       0x0D    /* Espressif / AP Memory */
#define PSRAM64H_KNOWN_KGD          0x5D    /* Known Good Die */

/*===========================================================================
 * 超时设置 (us / ms)
 *===========================================================================*/

#define PSRAM64H_TIMEOUT_RESET_US   150     /* 复位恢复时间 tRST (max 150us) */

/* DMA 单次最大传输字节数 (12-bit DMA block reg 上限为 4095，取 2048 安全) */
#define PSRAM64H_DMA_MAX_CHUNK      2048u

/*===========================================================================
 * ESP-PSRAM64H 驱动接口
 *===========================================================================*/

/**
 * @brief 创建 PSRAM64H 设备实例
 *
 * @param name        设备名称 (最长 15 字节)
 * @param cs_select   CS 拉低函数
 * @param cs_deselect CS 拉高函数
 * @param cs_init     CS 引脚初始化函数 (可为 NULL)
 * @return FlashDevice_t* 设备指针，失败返回 NULL
 */
FlashDevice_t* PSRAM64H_Create(const char *name,
                               void (*cs_select)(void),
                               void (*cs_deselect)(void),
                               void (*cs_init)(void));

/**
 * @brief 销毁 PSRAM64H 设备实例，释放内存
 * @param dev 设备指针
 */
void PSRAM64H_Destroy(FlashDevice_t *dev);

/**
 * @brief 获取 PSRAM64H 驱动操作表
 * @return const FlashOps_t* 操作表指针
 */
const FlashOps_t* PSRAM64H_GetOps(void);

/*===========================================================================
 * 底层直接操作 (供测试使用)
 *===========================================================================*/

/**
 * @brief 直接读取 PSRAM 数据
 * @param dev   设备指针
 * @param addr  24-bit 地址
 * @param buf   输出缓冲区
 * @param len   读取长度
 * @return FLASH_OK 成功
 */
FlashStatus_t PSRAM64H_DirectRead(FlashDevice_t *dev, uint32_t addr,
                                  uint8_t *buf, uint32_t len);

/**
 * @brief 直接写入 PSRAM 数据
 * @param dev   设备指针
 * @param addr  24-bit 地址
 * @param buf   数据指针
 * @param len   写入长度 (自动处理页边界绕回)
 * @return FLASH_OK 成功
 */
FlashStatus_t PSRAM64H_DirectWrite(FlashDevice_t *dev, uint32_t addr,
                                   const uint8_t *buf, uint32_t len);

/**
 * @brief 读取 PSRAM EID (Electronic ID)
 * @param dev      设备指针
 * @param mfg_id   输出制造商 ID
 * @param kgd      输出 KGD (Known Good Die)
 * @return FLASH_OK 成功
 */
FlashStatus_t PSRAM64H_ReadID(FlashDevice_t *dev, uint8_t *mfg_id, uint8_t *kgd);

/**
 * @brief 软件复位 PSRAM
 * @param dev 设备指针
 */
void PSRAM64H_Reset(FlashDevice_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* __PSRAM_ESP64H_H__ */
