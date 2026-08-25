/**
 *****************************************************************************
 * @file     flash_nand_w25n02.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    W25N02xx SPI NAND Flash 驱动 (含坏块管理)
 *
 * 支持型号: W25N02KV (2Gbit / 256MB)
 *
 * 规格:
 *   - 容量      : 2Gbit = 256MB
 *   - 页大小    : 2048 bytes (数据) + 64 bytes (spare/OOB)
 *   - 块大小    : 64 pages = 128KB
 *   - 块数量    : 2048 blocks
 *   - 页地址    : 16-bit (0x0000 ~ 0x07FF)
 *   - 接口      : Standard/Dual/Quad SPI
 *   - JEDEC ID  : EF AA 22
 *
 * 坏块管理 (BBM):
 *   - 初始化时扫描所有块的 OOB[0]，0xFF=好块，否则为坏块
 *   - RAM 中维护 2048-bit 位图 (256 bytes) 标记坏块
 *   - 擦除/编程失败时自动标记坏块
 *   - 提供 W25N02_IsBadBlock() / W25N02_MarkBadBlock() API
 *****************************************************************************
 */

#ifndef __FLASH_NAND_W25N02_H__
#define __FLASH_NAND_W25N02_H__

#include "flash_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * W25N02 规格常量
 *===========================================================================*/

#define W25N02_PAGE_SIZE            2048U       /* 数据页大小 (bytes) */
#define W25N02_OOB_SIZE             64U         /* spare/OOB 大小 (bytes) */
#define W25N02_PAGES_PER_BLOCK      64U         /* 每块页数 */
#define W25N02_BLOCK_COUNT          2048U       /* 总块数 */
#define W25N02_BLOCK_SIZE           (W25N02_PAGE_SIZE * W25N02_PAGES_PER_BLOCK)  /* 128KB */
#define W25N02_TOTAL_SIZE           ((uint32_t)W25N02_BLOCK_COUNT * W25N02_BLOCK_SIZE) /* 256MB */
#define W25N02_BBT_SIZE_BYTES       (W25N02_BLOCK_COUNT / 8U)  /* 坏块位图大小 (256 bytes) */

/*===========================================================================
 * W25N02 SPI 命令集
 *===========================================================================*/

#define W25N02_CMD_RESET            0xFF    /* 设备复位 */
#define W25N02_CMD_READ_JEDEC_ID    0x9F    /* 读取 JEDEC ID */
#define W25N02_CMD_READ_ID          0x90    /* 读取制造商/设备 ID */
#define W25N02_CMD_GET_FEATURE      0x0F    /* 读取特性寄存器 */
#define W25N02_CMD_SET_FEATURE      0x1F    /* 写特性寄存器 */
#define W25N02_CMD_WRITE_ENABLE     0x06    /* 写使能 */
#define W25N02_CMD_WRITE_DISABLE    0x04    /* 写禁止 */
#define W25N02_CMD_PAGE_READ        0x13    /* 读页到缓存 (page addr → cache) */
#define W25N02_CMD_READ_CACHE       0x03    /* 从缓存读数据 (slow) */
#define W25N02_CMD_READ_CACHE_FAST  0x0B    /* 从缓存快速读数据 */
#define W25N02_CMD_READ_CACHE_X2    0x3B    /* 双线缓存读 */
#define W25N02_CMD_READ_CACHE_X4    0x6B    /* 四线缓存读 */
#define W25N02_CMD_LOAD_PROG_DATA   0x02    /* 加载数据到缓存 (写入) */
#define W25N02_CMD_PROG_EXECUTE     0x10    /* 编程执行 (缓存 → Flash) */
#define W25N02_CMD_BLOCK_ERASE      0xD8    /* 块擦除 */
#define W25N02_CMD_READ_BBM_LUT     0xA5    /* 读取内置 BBM 查找表 */
#define W25N02_CMD_LAST_ECC_FAIL    0xA9    /* 获取最后 ECC 失败的页地址 */
#define W25N02_CMD_PROG_LOAD_RANDOM 0x84    /* 随机加载数据到缓存 */

/*===========================================================================
 * 特性寄存器地址
 *===========================================================================*/

#define W25N02_REG_PROTECTION       0xA0    /* 保护寄存器 */
#define W25N02_REG_CONFIG           0xB0    /* 配置寄存器 */
#define W25N02_REG_STATUS           0xC0    /* 状态寄存器 */

/*===========================================================================
 * 状态寄存器位定义 (addr 0xC0)
 *===========================================================================*/

#define W25N02_SR_OIP               (1 << 0)    /* 操作进行中 (BUSY) */
#define W25N02_SR_WEL               (1 << 1)    /* 写使能锁存 */
#define W25N02_SR_ERASE_FAIL        (1 << 2)    /* 擦除失败 */
#define W25N02_SR_PROG_FAIL         (1 << 3)    /* 编程失败 */
#define W25N02_SR_ECC_S0            (1 << 4)    /* ECC 状态位 0 */
#define W25N02_SR_ECC_S1            (1 << 5)    /* ECC 状态位 1 */

/* ECC 状态解码:
 * ECC_S1:ECC_S0 = 00 → 无错误
 * ECC_S1:ECC_S0 = 01 → 1~4 bit 错误已纠正
 * ECC_S1:ECC_S0 = 10 → 错误超过纠正能力 (ECC 失败)
 * ECC_S1:ECC_S0 = 11 → 保留
 */
#define W25N02_ECC_OK               (0x00)
#define W25N02_ECC_CORRECTED        (W25N02_SR_ECC_S0)
#define W25N02_ECC_UNCORRECTABLE    (W25N02_SR_ECC_S1)
#define W25N02_ECC_MASK             (W25N02_SR_ECC_S0 | W25N02_SR_ECC_S1)

/*===========================================================================
 * 配置寄存器位定义 (addr 0xB0)
 *===========================================================================*/

#define W25N02_CFG_ECC_EN           (1 << 4)    /* ECC 使能 (默认 1) */
#define W25N02_CFG_BUF_EN           (1 << 3)    /* 缓冲读模式 (1=buffer, 0=continuous) */

/*===========================================================================
 * 厂商 ID
 *===========================================================================*/

#define W25N02_MFG_WINBOND          0xEF        /* Winbond 厂商 ID */
#define W25N02_MEM_TYPE             0xAA        /* NAND Flash 类型 */
#define W25N02_DEV_ID               0x22        /* W25N02KV 设备 ID */

/*===========================================================================
 * 超时设置 (ms)
 *===========================================================================*/

#define W25N02_TIMEOUT_PAGE_READ    1           /* 页读取超时 (典型 <60us, 最大 115us) */
#define W25N02_TIMEOUT_PAGE_PROG    5           /* 页编程超时 (典型 300us, 最大 700us) */
#define W25N02_TIMEOUT_BLOCK_ERASE  10          /* 块擦除超时 (典型 2ms, 最大 5ms) */
#define W25N02_TIMEOUT_RESET        1           /* 复位超时 */

/*===========================================================================
 * 坏块管理 (BBM) 数据结构
 *===========================================================================*/

/**
 * @brief W25N02 坏块管理上下文
 */
typedef struct {
    uint8_t  bbt[W25N02_BBT_SIZE_BYTES];    /* 坏块位图: bit=1 表示坏块 */
    uint16_t bad_count;                      /* 坏块总数 */
    bool     scanned;                        /* 是否已完成扫描 */
} W25N02_BBM_t;

/*===========================================================================
 * W25N02 驱动接口
 *===========================================================================*/

/**
 * @brief 创建 W25N02 设备实例 (自动分配内存)
 *
 * @param name        设备名称 (最长 15 字节)
 * @param cs_select   CS 拉低函数
 * @param cs_deselect CS 拉高函数
 * @param cs_init     CS 引脚初始化函数 (可为 NULL)
 * @return FlashDevice_t* 设备指针，失败返回 NULL
 */
FlashDevice_t* W25N02_Create(const char *name,
                             void (*cs_select)(void),
                             void (*cs_deselect)(void),
                             void (*cs_init)(void));

/**
 * @brief 销毁 W25N02 设备实例，释放内存
 * @param dev 设备指针
 */
void W25N02_Destroy(FlashDevice_t *dev);

/**
 * @brief 获取 W25N02 驱动操作表
 * @return const FlashOps_t* 操作表指针
 */
const FlashOps_t* W25N02_GetOps(void);

/*===========================================================================
 * 坏块管理 API
 *===========================================================================*/

/**
 * @brief 扫描所有块，构建 RAM 坏块位图
 * @param dev 设备指针
 * @return FLASH_OK 成功
 */
/**
 * @brief 复位 NAND 设备并清除写保护（兼容第三方芯片）
 *        在每次测试开始前调用，保证芯片处于已知状态
 */
FlashStatus_t W25N02_ResetDevice(FlashDevice_t *dev);

FlashStatus_t W25N02_ScanBBT(FlashDevice_t *dev);

/**
 * @brief 判断某块是否为坏块
 * @param dev        设备指针
 * @param block_addr 块号 (0 ~ 2047)
 * @return true=坏块, false=好块
 */
bool W25N02_IsBadBlock(FlashDevice_t *dev, uint16_t block_addr);

/**
 * @brief 将某块标记为坏块 (更新 RAM 位图 + OOB 标记)
 * @param dev        设备指针
 * @param block_addr 块号
 * @return FLASH_OK 成功
 */
FlashStatus_t W25N02_MarkBadBlock(FlashDevice_t *dev, uint16_t block_addr);

/**
 * @brief 获取坏块统计信息
 * @param dev       设备指针
 * @param out_bbm   输出坏块管理上下文指针 (只读)
 * @return FLASH_OK 成功
 */
FlashStatus_t W25N02_GetBBM(FlashDevice_t *dev, const W25N02_BBM_t **out_bbm);

/*===========================================================================
 * 底层直接操作 (供测试使用)
 *===========================================================================*/

/**
 * @brief 直接读取页 (含 OOB)
 * @param dev      设备指针
 * @param page_addr 16-bit 页地址 (0 ~ 0x7FFF)
 * @param col_addr  列偏移 (0 ~ 2111)
 * @param buf      输出缓冲区
 * @param len      读取长度
 */
FlashStatus_t W25N02_ReadPage(FlashDevice_t *dev, uint32_t page_addr,
                              uint16_t col_addr, uint8_t *buf, uint32_t len);

/**
 * @brief 直接写入页 (col_addr 必须位于 [0, 2048) 且 col_addr+len <= 2048)
 * @param dev       设备指针
 * @param page_addr 16-bit 页地址
 * @param col_addr  列偏移
 * @param buf       数据缓冲区
 * @param len       写入长度
 */
FlashStatus_t W25N02_ProgramPage(FlashDevice_t *dev, uint32_t page_addr,
                                 uint16_t col_addr, const uint8_t *buf, uint32_t len);

/**
 * @brief 直接块擦除
 * @param dev        设备指针
 * @param block_addr 块号 (0 ~ 2047)
 */
FlashStatus_t W25N02_EraseBlock(FlashDevice_t *dev, uint16_t block_addr);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_NAND_W25N02_H__ */
