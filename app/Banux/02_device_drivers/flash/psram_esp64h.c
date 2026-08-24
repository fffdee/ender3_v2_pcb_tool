/**
 * psram_esp64h.c - ESP-PSRAM64H 驱动实现
 * 
 * 使用硬件 SPI (SPIM) + DMA 方式驱动外部 PSRAM
 * 参考 flash_nor_w25qxx.c 实现
 * 
 * ESP-PSRAM64H 特性:
 * - 容量: 8MB (64Mbit)
 * - 无需擦除 (RAM 特性)
 * - 无需写使能
 * - 无 WIP 等待
 */

#include "psram_esp64h.h"
#include "spim.h"
#include "spim_interface.h"
#include "dma.h"
#include "gpio.h"
#include "debug.h"
#include "rtos_api.h"
#include <string.h>
#include "rtos_api.h"

/*===========================================================================
 * 内部宏定义
 *===========================================================================*/

#define PSRAM64H_DEBUG    1

#if PSRAM64H_DEBUG
    #define PSRAM64H_LOG(fmt, ...)  DBG("[PSRAM64H] " fmt, ##__VA_ARGS__)
#else
    #define PSRAM64H_LOG(...)
#endif

/*===========================================================================
 * 函数前向声明
 *===========================================================================*/

static FlashStatus_t PSRAM64H_Init(FlashDevice_t *dev);
static FlashStatus_t PSRAM64H_DeInit(FlashDevice_t *dev);
static FlashStatus_t PSRAM64H_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len);
static FlashStatus_t PSRAM64H_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len);
static FlashStatus_t PSRAM64H_EraseSector(FlashDevice_t *dev, uint32_t addr);
static FlashStatus_t PSRAM64H_EraseBlock(FlashDevice_t *dev, uint32_t addr);
static FlashStatus_t PSRAM64H_EraseChip(FlashDevice_t *dev);
static FlashStatus_t PSRAM64H_GetStatus(FlashDevice_t *dev, uint8_t *status);
static FlashStatus_t PSRAM64H_WaitReady(FlashDevice_t *dev, uint32_t timeout_ms);
static FlashStatus_t PSRAM64H_ReadIDInternal(FlashDevice_t *dev);
static FlashStatus_t PSRAM64H_GetInfo(FlashDevice_t *dev, FlashDevInfo_t *info);

/*===========================================================================
 * 驱动操作表
 *===========================================================================*/

static const FlashOps_t g_psram64h_ops = {
    .init         = PSRAM64H_Init,
    .deinit       = PSRAM64H_DeInit,
    .read         = PSRAM64H_Read,
    .write        = PSRAM64H_Write,
    .erase_sector = PSRAM64H_EraseSector,  /* PSRAM 无需擦除，空实现 */
    .erase_block  = PSRAM64H_EraseBlock,
    .erase_chip   = PSRAM64H_EraseChip,
    .get_status   = PSRAM64H_GetStatus,
    .wait_ready   = PSRAM64H_WaitReady,    /* PSRAM 始终 ready */
    .read_id      = PSRAM64H_ReadIDInternal,
    .get_info     = PSRAM64H_GetInfo
};

const FlashOps_t* PSRAM64H_GetOps(void)
{
    return &g_psram64h_ops;
}

/*===========================================================================
 * 底层 SPI 操作 (使用 SPIM DMA)
 *===========================================================================*/

/* SPI DMA 等待超时计数：约 500000 次轮询 (~5ms @ 100MHz CPU)，防止 DMA
 * 通道冲突（如 SDIO BT 音频与 SPIM PSRAM 共享 DMA 通道时）导致死机。
 * 超时后安全返回，上层调用方需检查返回值并重试或静音。               */
#define PSRAM_SPI_DMA_TIMEOUT   500000u

/**
 * @brief SPI DMA 发送单字节
 */
static FlashStatus_t spi_write_byte(uint8_t data)
{
    uint32_t timeout = PSRAM_SPI_DMA_TIMEOUT;
    SPIM_DMA_Send_Start(&data, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX)) {
        if (--timeout == 0u) { return FLASH_ERR_TIMEOUT; }
    }
    return FLASH_OK;
}

/**
 * @brief SPI DMA 接收单字节
 */
static FlashStatus_t spi_read_byte(uint8_t *out)
{
    uint32_t timeout = PSRAM_SPI_DMA_TIMEOUT;
    SPIM_DMA_Recv_Start(out, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX)) {
        if (--timeout == 0u) { return FLASH_ERR_TIMEOUT; }
    }
    return FLASH_OK;
}

/**
 * @brief SPI DMA 发送多字节
 */
static FlashStatus_t spi_write(uint8_t *data, uint16_t size)
{
    uint32_t timeout = PSRAM_SPI_DMA_TIMEOUT;
    SPIM_DMA_Send_Start(data, size);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX)) {
        if (--timeout == 0u) { return FLASH_ERR_TIMEOUT; }
    }
    return FLASH_OK;
}

/**
 * @brief SPI DMA 接收多字节
 */
static FlashStatus_t spi_read(uint8_t *data, uint16_t size)
{
    uint32_t timeout = PSRAM_SPI_DMA_TIMEOUT;
    SPIM_DMA_Recv_Start(data, size);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX)) {
        if (--timeout == 0u) { return FLASH_ERR_TIMEOUT; }
    }
    return FLASH_OK;
}

/**
 * @brief 发送命令+地址 (24-bit)
 */
static FlashStatus_t psram64h_cmd_addr(FlashDevice_t *dev, uint8_t cmd, uint32_t addr)
{
    (void)dev;
    if (spi_write_byte(cmd)                              != FLASH_OK) return FLASH_ERR_TIMEOUT;
    if (spi_write_byte((uint8_t)((addr >> 16) & 0xFF))   != FLASH_OK) return FLASH_ERR_TIMEOUT;
    if (spi_write_byte((uint8_t)((addr >> 8)  & 0xFF))   != FLASH_OK) return FLASH_ERR_TIMEOUT;
    if (spi_write_byte((uint8_t)(addr         & 0xFF))   != FLASH_OK) return FLASH_ERR_TIMEOUT;
    return FLASH_OK;
}

/*===========================================================================
 * 驱动实现
 *===========================================================================*/

static FlashStatus_t PSRAM64H_Init(FlashDevice_t *dev)
{
    uint8_t mfg_id, kgd;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    /* 初始化 CS 引脚 */
    if (dev->cs.init) {
        dev->cs.init();
    }
    if (dev->cs.deselect) {
        dev->cs.deselect();
    }
    
    /* 软件复位 PSRAM */
    PSRAM64H_Reset(dev);
    
    /* 读取 ID */
    if (PSRAM64H_ReadID(dev, &mfg_id, &kgd) != FLASH_OK) {
        PSRAM64H_LOG("Failed to read ID\n");
        return FLASH_ERR_NOT_FOUND;
    }
    
    dev->info.mfg_id   = mfg_id;
    dev->info.mem_type = 0;  /* PSRAM 无 mem_type */
    dev->info.dev_id   = kgd;

    if ((mfg_id == 0x00u && kgd == 0x00u) ||
        (mfg_id == 0xFFu && kgd == 0xFFu) ||
        kgd != PSRAM64H_KNOWN_KGD) {
        PSRAM64H_LOG("No valid ESP-PSRAM64H ID detected: MFG=0x%02X KGD=0x%02X\n",
                     mfg_id, kgd);
        return FLASH_ERR_NOT_FOUND;
    }
    
    /* 验证 ID */
    if (mfg_id != PSRAM64H_KNOWN_MFG_ID) {
        /* Mfg=0x06 = IPUS/APM/Lyontek 兼容 PSRAM，命令集与 ESP-PSRAM64H 相同 */
        PSRAM64H_LOG("Compatible PSRAM detected: Mfg=0x%02X KGD=0x%02X\n",
                     mfg_id, kgd);
    }
    
    /* 设置设备参数 */
    dev->info.total_size  = PSRAM64H_TOTAL_SIZE;
    dev->info.page_size   = PSRAM64H_PAGE_SIZE;
    dev->info.sector_size = PSRAM64H_SECTOR_SIZE;
    dev->info.block_size  = PSRAM64H_BLOCK_SIZE;
    dev->info.block_count = PSRAM64H_BLOCK_COUNT;
    
    PSRAM64H_LOG("Init OK: MFG=0x%02X KGD=0x%02X Size=%dMB\n",
                 mfg_id, kgd, PSRAM64H_TOTAL_SIZE / (1024*1024));
    
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_DeInit(FlashDevice_t *dev)
{
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    /* PSRAM 无特殊去初始化操作 */
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
    return PSRAM64H_DirectRead(dev, addr, buf, len);
}

static FlashStatus_t PSRAM64H_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    return PSRAM64H_DirectWrite(dev, addr, buf, len);
}

static FlashStatus_t PSRAM64H_EraseSector(FlashDevice_t *dev, uint32_t addr)
{
    /* PSRAM 无需擦除，直接返回成功 */
    (void)dev;
    (void)addr;
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_EraseBlock(FlashDevice_t *dev, uint32_t addr)
{
    /* PSRAM 无需擦除，直接返回成功 */
    (void)dev;
    (void)addr;
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_EraseChip(FlashDevice_t *dev)
{
    /* PSRAM 无需擦除，直接返回成功 */
    (void)dev;
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_GetStatus(FlashDevice_t *dev, uint8_t *status)
{
    if (!dev || !status) {
        return FLASH_ERR_PARAM;
    }
    
    /* PSRAM 始终 ready，无 WIP 位 */
    *status = 0;
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_WaitReady(FlashDevice_t *dev, uint32_t timeout_ms)
{
    /* PSRAM 无需等待，始终 ready */
    (void)dev;
    (void)timeout_ms;
    return FLASH_OK;
}

static FlashStatus_t PSRAM64H_ReadIDInternal(FlashDevice_t *dev)
{
    uint8_t mfg_id, kgd;
    return PSRAM64H_ReadID(dev, &mfg_id, &kgd);
}

static FlashStatus_t PSRAM64H_GetInfo(FlashDevice_t *dev, FlashDevInfo_t *info)
{
    if (!dev || !info) {
        return FLASH_ERR_PARAM;
    }
    
    *info = dev->info;
    return FLASH_OK;
}

/*===========================================================================
 * 公共 API
 *===========================================================================*/

FlashDevice_t* PSRAM64H_Create(const char *name,
                               void (*cs_select)(void),
                               void (*cs_deselect)(void),
                               void (*cs_init)(void))
{
    FlashDevice_t *dev;
    
    if (!name || !cs_select || !cs_deselect) {
        PSRAM64H_LOG("Create failed: invalid parameters\n");
        return NULL;
    }
    
    dev = (FlashDevice_t *)pvPortMalloc(sizeof(FlashDevice_t));
    if (!dev) {
        PSRAM64H_LOG("Create failed: out of memory\n");
        return NULL;
    }
    
    memset(dev, 0, sizeof(FlashDevice_t));
    
    /* 设置设备名称 */
    strncpy(dev->name, name, FLASH_NAME_MAX_LEN - 1);
    dev->name[FLASH_NAME_MAX_LEN - 1] = '\0';
    
    /* 设置 CS 控制 */
    dev->cs.init      = cs_init;
    dev->cs.select    = cs_select;
    dev->cs.deselect  = cs_deselect;
    
    /* 设置类型和操作表 */
    dev->type = FLASH_TYPE_PSRAM;
    dev->ops  = &g_psram64h_ops;
    
    return dev;
}

void PSRAM64H_Destroy(FlashDevice_t *dev)
{
    if (dev) {
        vPortFree(dev);
    }
}

/*===========================================================================
 * 底层直接操作
 *===========================================================================*/

FlashStatus_t PSRAM64H_DirectRead(FlashDevice_t *dev, uint32_t addr,
                                  uint8_t *buf, uint32_t len)
{
    if (!dev || !buf || len == 0) {
        return FLASH_ERR_PARAM;
    }
    
    if (addr + len > PSRAM64H_TOTAL_SIZE) {
        return FLASH_ERR_PARAM;
    }
    
    dev->cs.select();
    
    /* 使用快速读命令 0x0B + 8 dummy clocks。
     * 原 Slow Read 0x03 存在 1-bit 移位问题：PSRAM 芯片需要额外
     * 建立时间，导致 MISO 输出延迟 1 个时钟。Fast Read 的 8 个
     * dummy clock 提供了足够的建立时间，消除数据移位。
     * 将 cmd+addr+dummy 合并为一次 5 字节 DMA 发送，避免
     * 字节间 SPIM 空闲时钟干扰。 */
    {
        FlashStatus_t ret;
        uint8_t cmd_buf[5];
        cmd_buf[0] = PSRAM64H_CMD_FAST_READ;
        cmd_buf[1] = (addr >> 16) & 0xFF;
        cmd_buf[2] = (addr >> 8)  & 0xFF;
        cmd_buf[3] = addr         & 0xFF;
        cmd_buf[4] = 0xFF;  /* dummy byte (8 clocks) */
        ret = spi_write(cmd_buf, 5);
        if (ret != FLASH_OK) { dev->cs.deselect(); return ret; }
    }
    
    /* 分块读取：DMA block 寄存器疑为 12-bit，超过 4095 字节会溢出。
     * 使用 2048 字节/块（与 NAND 页大小一致，已验证可用）。
     * 当 len <= 4 时使用 spi_read_byte 逐字节读取，
     * 避免 SPIM DMA 极短传输时 HalfDone 标志与 CS 控制的竞争。
     * 在同一个 CS 事务内连续读取，PSRAM 自动递增地址。 */
    if (len <= 4u) {
        uint32_t k;
        for (k = 0; k < len; k++) {
            FlashStatus_t ret = spi_read_byte(&buf[k]);
            if (ret != FLASH_OK) { dev->cs.deselect(); return ret; }
        }
    } else {
        uint32_t remaining = len;
        uint8_t *p = buf;
        uint32_t chunk;
        while (remaining > 0u) {
            FlashStatus_t ret;
            chunk = (remaining > PSRAM64H_DMA_MAX_CHUNK)
                    ? PSRAM64H_DMA_MAX_CHUNK : remaining;
            ret = spi_read(p, (uint16_t)chunk);
            if (ret != FLASH_OK) { dev->cs.deselect(); return ret; }
            p         += chunk;
            remaining -= chunk;
        }
    }
    
    dev->cs.deselect();
    
    return FLASH_OK;
}

FlashStatus_t PSRAM64H_DirectWrite(FlashDevice_t *dev, uint32_t addr,
                                   const uint8_t *buf, uint32_t len)
{
    uint32_t page_remain;
    uint32_t write_len;
    
    if (!dev || !buf || len == 0) {
        return FLASH_ERR_PARAM;
    }
    
    if (addr + len > PSRAM64H_TOTAL_SIZE) {
        return FLASH_ERR_PARAM;
    }
    
    /* PSRAM 写入会在页边界 (1KB) 处绕回，需要分段写入 */
    while (len > 0) {
        page_remain = PSRAM64H_PAGE_SIZE - (addr % PSRAM64H_PAGE_SIZE);
        write_len = (len < page_remain) ? len : page_remain;
        
        dev->cs.select();
        
        /* 发送写命令+地址 */
        if (psram64h_cmd_addr(dev, PSRAM64H_CMD_WRITE, addr) != FLASH_OK) {
            dev->cs.deselect();
            return FLASH_ERR_TIMEOUT;
        }
        
        /* 写入数据。
         * 当 write_len <= 4 时使用 spi_write_byte 逐字节发送，
         * 避免 SPIM DMA 极短传输时 HalfDone 标志提前置位导致 CS 过早拉高。
         * 大块数据仍走 DMA 以保持高吸吐。 */
        if (write_len <= 4u) {
            uint32_t k;
            for (k = 0; k < write_len; k++) {
                if (spi_write_byte(buf[k]) != FLASH_OK) {
                    dev->cs.deselect();
                    return FLASH_ERR_TIMEOUT;
                }
            }
        } else {
            if (spi_write((uint8_t *)buf, (uint16_t)write_len) != FLASH_OK) {
                dev->cs.deselect();
                return FLASH_ERR_TIMEOUT;
            }
        }
        
        dev->cs.deselect();
        
        addr += write_len;
        buf  += write_len;
        len  -= write_len;
    }
    
    return FLASH_OK;
}

FlashStatus_t PSRAM64H_ReadID(FlashDevice_t *dev, uint8_t *mfg_id, uint8_t *kgd)
{
    uint8_t id[6];
    uint8_t i;

    if (!dev || !mfg_id || !kgd) {
        return FLASH_ERR_PARAM;
    }

    dev->cs.select();

    /* 发送读 ID 命令 0x9F + 24-bit dummy address
     * 合并为一次 4 字节 DMA burst，避免字节间 SPIM 空闲时钟导致
     * 1-bit 移位 (与 DirectRead 同理) */
    {
        FlashStatus_t ret;
        uint8_t cmd_buf[4];
        cmd_buf[0] = PSRAM64H_CMD_READ_ID;
        cmd_buf[1] = 0x00;
        cmd_buf[2] = 0x00;
        cmd_buf[3] = 0x00;
        ret = spi_write(cmd_buf, 4);
        if (ret != FLASH_OK) { dev->cs.deselect(); return ret; }
    }

    /* 逐字节读取 6 字节响应（避免 DMA 批量读取的缓存一致性问题）:
     * Byte0 = MFG_ID, Byte1 = KGD (Known Good Die), Byte2~5 = EID */
    for (i = 0; i < 6u; i++) {
        if (spi_read_byte(&id[i]) != FLASH_OK) { dev->cs.deselect(); return FLASH_ERR_TIMEOUT; }
    }

    dev->cs.deselect();

    PSRAM64H_LOG("ReadID raw: %02X %02X %02X %02X %02X %02X\n",
                 id[0], id[1], id[2], id[3], id[4], id[5]);

    *mfg_id = id[0];  /* Manufacturer ID */
    *kgd    = id[1];  /* Known Good Die  */

    return FLASH_OK;
}

void PSRAM64H_Reset(FlashDevice_t *dev)
{
    volatile uint32_t i;
    
    if (!dev) {
        return;
    }
    
    /* Exit QPI mode first in case chip was left in QPI state */
    dev->cs.select();
    (void)spi_write_byte(PSRAM64H_CMD_EXIT_QPI);
    dev->cs.deselect();
    for (i = 0; i < 100; i++);
    
    /* 发送复位使能命令 */
    dev->cs.select();
    (void)spi_write_byte(PSRAM64H_CMD_RESET_ENABLE);
    dev->cs.deselect();
    
    /* 短延时 */
    for (i = 0; i < 100; i++);
    
    /* 发送复位命令 */
    dev->cs.select();
    (void)spi_write_byte(PSRAM64H_CMD_RESET);
    dev->cs.deselect();
    
    /* 等待复位完成 (tRST = 150us, 约 15000 循环 @ 100MHz) */
    for (i = 0; i < 15000; i++);
}
