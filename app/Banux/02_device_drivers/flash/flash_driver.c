/**
 * flash_driver.c - Flash底层驱动实现
 * 
 * 支持多颗NOR Flash (W25Q64) 通过不同CS引脚控制
 */

#include "flash_driver.h"
#include "spim.h"
#include "spim_interface.h"
#include "dma.h"
#include "debug.h"
#include <string.h>
#include <stdlib.h>

/*===========================================================================
 * 底层SPI通信
 *===========================================================================*/

void flash_spi_write_byte(uint8_t data)
{
    SPIM_DMA_Send_Start(&data, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

uint8_t flash_spi_read_byte(void)
{
    uint8_t data;
    SPIM_DMA_Recv_Start(&data, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
    return data;
}

void flash_spi_write(const uint8_t *data, uint16_t len)
{
    SPIM_DMA_Send_Start((uint8_t*)data, len);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

void flash_spi_read(uint8_t *data, uint16_t len)
{
    SPIM_DMA_Recv_Start(data, len);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
}

/*===========================================================================
 * NOR Flash驱动私有数据
 *===========================================================================*/

typedef struct {
    FlashCsFunc_t cs_enable;
    FlashCsFunc_t cs_disable;
} NorFlashPriv_t;

/*===========================================================================
 * NOR Flash内部函数
 *===========================================================================*/

static inline void nor_cs_enable(FlashDriver_t *drv)
{
    NorFlashPriv_t *priv = (NorFlashPriv_t*)drv->priv;
    if (priv && priv->cs_enable) {
        priv->cs_enable(true);
    }
}

static inline void nor_cs_disable(FlashDriver_t *drv)
{
    NorFlashPriv_t *priv = (NorFlashPriv_t*)drv->priv;
    if (priv) {
        if (priv->cs_disable) {
            priv->cs_disable(true);
        } else if (priv->cs_enable) {
            priv->cs_enable(false);
        }
    }
}

static void nor_write_enable(FlashDriver_t *drv)
{
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_WRITE_ENABLE);
    nor_cs_disable(drv);
}

static void nor_write_disable(FlashDriver_t *drv)
{
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_WRITE_DISABLE);
    nor_cs_disable(drv);
}

/*===========================================================================
 * NOR Flash驱动操作实现
 *===========================================================================*/

static FlashStatus_t nor_init(FlashDriver_t *drv)
{
    uint8_t mfg, type, dev;
    
    if (!drv) return FLASH_ERROR_PARAM;
    
    /* 读取设备ID */
    drv->read_id(drv, &mfg, &type, &dev);
    
    /* 填充设备信息 */
    drv->info.type = FLASH_TYPE_NOR;
    drv->info.manufacturer_id = mfg;
    drv->info.memory_type = type;
    drv->info.device_id = dev;
    drv->info.page_size = NOR_PAGE_SIZE;
    drv->info.sector_size = NOR_SECTOR_SIZE_4K;
    drv->info.block_size = NOR_BLOCK_SIZE_64K;
    
    /* 根据设备ID确定容量 */
    switch (dev) {
        case 0x17: /* W25Q64 - 64Mbit = 8MB */
            drv->info.total_size = 8 * 1024 * 1024;
            drv->info.model = FLASH_MODEL_W25Q64;
            break;
        case 0x18: /* W25Q128 - 128Mbit = 16MB */
            drv->info.total_size = 16 * 1024 * 1024;
            drv->info.model = FLASH_MODEL_W25Q128;
            break;
        case 0x16: /* W25Q32 - 32Mbit = 4MB */
            drv->info.total_size = 4 * 1024 * 1024;
            drv->info.model = FLASH_MODEL_W25Q32;
            break;
        default:
            drv->info.total_size = 8 * 1024 * 1024; /* 默认8MB */
            drv->info.model = FLASH_MODEL_W25Q64;
            break;
    }
    
    drv->info.block_count = drv->info.total_size / drv->info.block_size;
    drv->initialized = true;
    
    DBG("NOR Flash #%d Init: Mfg=0x%02X Type=0x%02X Dev=0x%02X Size=%dMB\n",
        drv->id, mfg, type, dev, drv->info.total_size / (1024*1024));
    
    return FLASH_OK;
}

static FlashStatus_t nor_deinit(FlashDriver_t *drv)
{
    if (!drv) return FLASH_ERROR_PARAM;
    drv->initialized = false;
    return FLASH_OK;
}

static FlashStatus_t nor_read_id(FlashDriver_t *drv, uint8_t *mfg, uint8_t *type, uint8_t *dev)
{
    if (!drv) return FLASH_ERROR_PARAM;
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_READ_JEDEC_ID);
    *mfg = flash_spi_read_byte();
    *type = flash_spi_read_byte();
    *dev = flash_spi_read_byte();
    nor_cs_disable(drv);
    
    return FLASH_OK;
}

static FlashStatus_t nor_get_status(FlashDriver_t *drv, uint8_t *status)
{
    if (!drv || !status) return FLASH_ERROR_PARAM;
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_READ_STATUS);
    *status = flash_spi_read_byte();
    nor_cs_disable(drv);
    
    return FLASH_OK;
}

static FlashStatus_t nor_wait_ready(FlashDriver_t *drv, uint32_t timeout_ms)
{
    uint8_t status;
    uint32_t count = 0;
    uint32_t max_count = timeout_ms * 1000; /* 简单计数 */
    
    if (!drv) return FLASH_ERROR_PARAM;
    
    do {
        nor_get_status(drv, &status);
        if (!(status & NOR_STATUS_BUSY)) {
            return FLASH_OK;
        }
        count++;
    } while (count < max_count);
    
    return FLASH_ERROR_TIMEOUT;
}

static FlashStatus_t nor_read(FlashDriver_t *drv, uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!drv || !buf) return FLASH_ERROR_PARAM;
    if (!drv->initialized) return FLASH_ERROR_NOT_INIT;
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_READ_DATA);
    flash_spi_write_byte((addr >> 16) & 0xFF);
    flash_spi_write_byte((addr >> 8) & 0xFF);
    flash_spi_write_byte(addr & 0xFF);
    flash_spi_read(buf, len);
    nor_cs_disable(drv);
    
    return FLASH_OK;
}

static FlashStatus_t nor_write(FlashDriver_t *drv, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t page_offset;
    uint32_t page_remain;
    uint32_t write_len;
    
    if (!drv || !buf) return FLASH_ERROR_PARAM;
    if (!drv->initialized) return FLASH_ERROR_NOT_INIT;
    
    while (len > 0) {
        /* 计算当前页内偏移和剩余空间 */
        page_offset = addr % NOR_PAGE_SIZE;
        page_remain = NOR_PAGE_SIZE - page_offset;
        write_len = (len < page_remain) ? len : page_remain;
        
        /* 写使能 */
        nor_write_enable(drv);
        
        /* 页编程 */
        nor_cs_enable(drv);
        flash_spi_write_byte(NOR_CMD_PAGE_PROGRAM);
        flash_spi_write_byte((addr >> 16) & 0xFF);
        flash_spi_write_byte((addr >> 8) & 0xFF);
        flash_spi_write_byte(addr & 0xFF);
        flash_spi_write(buf, write_len);
        nor_cs_disable(drv);
        
        /* 等待完成 */
        nor_wait_ready(drv, 10);
        
        addr += write_len;
        buf += write_len;
        len -= write_len;
    }
    
    return FLASH_OK;
}

static FlashStatus_t nor_erase_sector(FlashDriver_t *drv, uint32_t addr)
{
    if (!drv) return FLASH_ERROR_PARAM;
    if (!drv->initialized) return FLASH_ERROR_NOT_INIT;
    
    /* 对齐到扇区边界 */
    addr &= ~(NOR_SECTOR_SIZE_4K - 1);
    
    nor_write_enable(drv);
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_SECTOR_ERASE_4K);
    flash_spi_write_byte((addr >> 16) & 0xFF);
    flash_spi_write_byte((addr >> 8) & 0xFF);
    flash_spi_write_byte(addr & 0xFF);
    nor_cs_disable(drv);
    
    nor_wait_ready(drv, 500);
    
    return FLASH_OK;
}

static FlashStatus_t nor_erase_block(FlashDriver_t *drv, uint32_t addr)
{
    if (!drv) return FLASH_ERROR_PARAM;
    if (!drv->initialized) return FLASH_ERROR_NOT_INIT;
    
    /* 对齐到块边界 */
    addr &= ~(NOR_BLOCK_SIZE_64K - 1);
    
    nor_write_enable(drv);
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_BLOCK_ERASE_64K);
    flash_spi_write_byte((addr >> 16) & 0xFF);
    flash_spi_write_byte((addr >> 8) & 0xFF);
    flash_spi_write_byte(addr & 0xFF);
    nor_cs_disable(drv);
    
    nor_wait_ready(drv, 2000);
    
    return FLASH_OK;
}

static FlashStatus_t nor_erase_chip(FlashDriver_t *drv)
{
    if (!drv) return FLASH_ERROR_PARAM;
    if (!drv->initialized) return FLASH_ERROR_NOT_INIT;
    
    nor_write_enable(drv);
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_CHIP_ERASE);
    nor_cs_disable(drv);
    
    nor_wait_ready(drv, 60000); /* 全片擦除可能需要较长时间 */
    
    return FLASH_OK;
}

static FlashStatus_t nor_power_down(FlashDriver_t *drv)
{
    if (!drv) return FLASH_ERROR_PARAM;
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_POWER_DOWN);
    nor_cs_disable(drv);
    
    return FLASH_OK;
}

static FlashStatus_t nor_power_up(FlashDriver_t *drv)
{
    if (!drv) return FLASH_ERROR_PARAM;
    
    nor_cs_enable(drv);
    flash_spi_write_byte(NOR_CMD_RELEASE_PD);
    nor_cs_disable(drv);
    
    /* 等待唤醒 */
    volatile int delay = 1000;
    while (delay--);
    
    return FLASH_OK;
}

/*===========================================================================
 * NOR Flash驱动实例池
 *===========================================================================*/

#define MAX_NOR_FLASH_DEVICES 4

static FlashDriver_t nor_flash_drivers[MAX_NOR_FLASH_DEVICES];
static NorFlashPriv_t nor_flash_privs[MAX_NOR_FLASH_DEVICES];
static uint8_t nor_flash_count = 0;

/*===========================================================================
 * 创建NOR Flash驱动
 *===========================================================================*/

FlashDriver_t* FlashDriver_CreateNOR(uint8_t id, FlashCsFunc_t cs_enable, FlashCsFunc_t cs_disable)
{
    if (nor_flash_count >= MAX_NOR_FLASH_DEVICES) {
        DBG("Error: Max NOR Flash devices reached\n");
        return NULL;
    }
    
    FlashDriver_t *drv = &nor_flash_drivers[nor_flash_count];
    NorFlashPriv_t *priv = &nor_flash_privs[nor_flash_count];
    
    memset(drv, 0, sizeof(FlashDriver_t));
    memset(priv, 0, sizeof(NorFlashPriv_t));
    
    /* 设置私有数据 */
    priv->cs_enable = cs_enable;
    priv->cs_disable = cs_disable;
    
    /* 设置驱动 */
    drv->id = id;
    drv->type = FLASH_TYPE_NOR;
    drv->initialized = false;
    drv->priv = priv;
    
    /* 绑定操作函数 */
    drv->init = nor_init;
    drv->deinit = nor_deinit;
    drv->read_id = nor_read_id;
    drv->read = nor_read;
    drv->write = nor_write;
    drv->erase_sector = nor_erase_sector;
    drv->erase_block = nor_erase_block;
    drv->erase_chip = nor_erase_chip;
    drv->get_status = nor_get_status;
    drv->wait_ready = nor_wait_ready;
    drv->power_down = nor_power_down;
    drv->power_up = nor_power_up;
    
    nor_flash_count++;
    
    return drv;
}

void FlashDriver_Destroy(FlashDriver_t *drv)
{
    if (drv) {
        drv->initialized = false;
        /* 静态分配，不需要释放内存 */
    }
}
