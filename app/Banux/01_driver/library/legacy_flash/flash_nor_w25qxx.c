/**
 * flash_nor_w25qxx.c - W25Qxx系列NOR Flash驱动实现
 * 
 * 使用硬件SPI (SPIM) + DMA方式驱动外部Flash
 * 参考 BG_card_RTOS 项目的 bg_flash_manager.c 实现
 */

#include "flash_nor_w25qxx.h"
#include "spim.h"
#include "spim_interface.h"
#include "dma.h"
#include "gpio.h"
#include "debug.h"
#include "rtos_api.h"
#include <string.h>
#include <stdlib.h>

/*===========================================================================
 * 内部宏定义
 *===========================================================================*/

#define W25QXX_DEBUG    1

#if W25QXX_DEBUG
    #define W25QXX_LOG(fmt, ...)  DBG("[W25Qxx] " fmt, ##__VA_ARGS__)
#else
    #define W25QXX_LOG(...)
#endif

/*===========================================================================
 * 函数前向声明
 *===========================================================================*/

static FlashStatus_t W25Qxx_Init(FlashDevice_t *dev);
static FlashStatus_t W25Qxx_DeInit(FlashDevice_t *dev);
static FlashStatus_t W25Qxx_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len);
static FlashStatus_t W25Qxx_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len);
static FlashStatus_t W25Qxx_EraseSector(FlashDevice_t *dev, uint32_t addr);
static FlashStatus_t W25Qxx_EraseBlock(FlashDevice_t *dev, uint32_t addr);
static FlashStatus_t W25Qxx_EraseChip(FlashDevice_t *dev);
static FlashStatus_t W25Qxx_GetStatus(FlashDevice_t *dev, uint8_t *status);
static FlashStatus_t W25Qxx_WaitReady(FlashDevice_t *dev, uint32_t timeout_ms);
static FlashStatus_t W25Qxx_ReadID(FlashDevice_t *dev);
static FlashStatus_t W25Qxx_GetInfo(FlashDevice_t *dev, FlashDevInfo_t *info);

/*===========================================================================
 * 驱动操作表
 *===========================================================================*/

static const FlashOps_t g_w25qxx_ops = {
    .init         = W25Qxx_Init,
    .deinit       = W25Qxx_DeInit,
    .read         = W25Qxx_Read,
    .write        = W25Qxx_Write,
    .erase_sector = W25Qxx_EraseSector,
    .erase_block  = W25Qxx_EraseBlock,
    .erase_chip   = W25Qxx_EraseChip,
    .get_status   = W25Qxx_GetStatus,
    .wait_ready   = W25Qxx_WaitReady,
    .read_id      = W25Qxx_ReadID,
    .get_info     = W25Qxx_GetInfo
};

const FlashOps_t* W25Qxx_GetOps(void)
{
    return &g_w25qxx_ops;
}

/*===========================================================================
 * 底层SPI操作 (使用SPIM DMA)
 *===========================================================================*/

/**
 * @brief SPI DMA发送单字节
 */
static void spi_write_byte(uint8_t data)
{
    SPIM_DMA_Send_Start(&data, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

/**
 * @brief SPI DMA接收单字节
 */
static uint8_t spi_read_byte(void)
{
    uint8_t data;
    SPIM_DMA_Recv_Start(&data, 1);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
    return data;
}

/**
 * @brief SPI DMA发送多字节
 */
static void spi_write(uint8_t *data, uint16_t size)
{
    SPIM_DMA_Send_Start(data, size);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

/**
 * @brief SPI DMA接收多字节
 */
static void spi_read(uint8_t *data, uint16_t size)
{
    SPIM_DMA_Recv_Start(data, size);
    while (!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
}

/**
 * @brief 发送命令
 */
static void w25qxx_send_cmd(FlashDevice_t *dev, uint8_t cmd)
{
    dev->cs.select();
    spi_write_byte(cmd);
    dev->cs.deselect();
}

/**
 * @brief 发送命令并读取数据
 */
static void w25qxx_cmd_read(FlashDevice_t *dev, uint8_t cmd, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    
    dev->cs.select();
    spi_write_byte(cmd);
    for (i = 0; i < len; i++) {
        buf[i] = spi_read_byte();
    }
    dev->cs.deselect();
}

/**
 * @brief 发送命令+地址
 */
static void w25qxx_cmd_addr(FlashDevice_t *dev, uint8_t cmd, uint32_t addr)
{
    spi_write_byte(cmd);
    spi_write_byte((addr >> 16) & 0xFF);
    spi_write_byte((addr >> 8) & 0xFF);
    spi_write_byte(addr & 0xFF);
}

/**
 * @brief 写使能
 */
static void w25qxx_write_enable(FlashDevice_t *dev)
{
    w25qxx_send_cmd(dev, W25QXX_CMD_WRITE_ENABLE);
}

/**
 * @brief 写使能 + WEL 验证 (调试用)
 * @return FLASH_OK if WEL is set, FLASH_ERR_WRITE otherwise
 */
static FlashStatus_t w25qxx_write_enable_verify(FlashDevice_t *dev)
{
    uint8_t sr;
    w25qxx_write_enable(dev);
    w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);
    if (!(sr & W25QXX_SR1_WEL)) {
        W25QXX_LOG("WREN FAILED! SR1=0x%02X (WEL=0)\n", sr);
        return FLASH_ERR_WRITE;
    }
    return FLASH_OK;
}

/*===========================================================================
 * 驱动实现
 *===========================================================================*/

static FlashStatus_t W25Qxx_Init(FlashDevice_t *dev)
{
    uint8_t id[3];
    uint32_t total_size;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    /* 初始化CS引脚 */
    if (dev->cs.init) {
        dev->cs.init();
    }
    if (dev->cs.deselect) {
        dev->cs.deselect();
    }
    
    /* 注意：SPIM已在系统初始化时配置好，这里只需要初始化CS引脚 */
    
    /* 读取JEDEC ID */
    w25qxx_cmd_read(dev, W25QXX_CMD_READ_JEDEC_ID, id, 3);
    
    dev->info.mfg_id   = id[0];
    dev->info.mem_type = id[1];
    dev->info.dev_id   = id[2];

    if ((id[0] == 0x00u && id[1] == 0x00u && id[2] == 0x00u) ||
        (id[0] == 0xFFu && id[1] == 0xFFu && id[2] == 0xFFu)) {
        W25QXX_LOG("No valid JEDEC ID detected: %02X %02X %02X\n",
                   id[0], id[1], id[2]);
        return FLASH_ERR_NOT_FOUND;
    }
    
    /* 验证厂商ID */
    if (dev->info.mfg_id != W25QXX_MFG_WINBOND) {
        W25QXX_LOG("Unknown manufacturer: 0x%02X\n", dev->info.mfg_id);
        /* 可能是兼容芯片，继续尝试 */
    }
    
    /* 根据设备ID确定容量 */
    switch (dev->info.dev_id) {
        case W25QXX_DEV_Q32:  total_size = 4 * 1024 * 1024;  break;
        case W25QXX_DEV_Q64:  total_size = 8 * 1024 * 1024;  break;
        case W25QXX_DEV_Q128: total_size = 16 * 1024 * 1024; break;
        case W25QXX_DEV_Q256: total_size = 32 * 1024 * 1024; break;
        default:
            W25QXX_LOG("Unknown device ID: 0x%02X\n", dev->info.dev_id);
            return FLASH_ERR_NOT_FOUND;
    }
    
    dev->info.page_size   = W25QXX_PAGE_SIZE;
    dev->info.sector_size = W25QXX_SECTOR_SIZE;
    dev->info.block_size  = W25QXX_BLOCK_SIZE_64K;
    dev->info.total_size  = total_size;
    
    /* Clear Block Protect bits in Status Register to allow writes/erases.
     * PY25Q128 and compatible chips may power up with BP bits set. */
    {
        uint8_t sr;
        w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);
        W25QXX_LOG("SR1 = 0x%02X\n", sr);
        if (sr & (W25QXX_SR1_BP0 | W25QXX_SR1_BP1 | W25QXX_SR1_BP2
                | W25QXX_SR1_TB  | W25QXX_SR1_SEC)) {
            W25QXX_LOG("Clearing BP bits (SR1=0x%02X)...\n", sr);
            w25qxx_write_enable(dev);
            dev->cs.select();
            spi_write_byte(W25QXX_CMD_WRITE_STATUS_REG);
            spi_write_byte(0x00);  /* SR1: clear all protect bits */
            dev->cs.deselect();
            /* Wait for write-status-register to complete (typ. 10-15ms) */
            {
                volatile uint32_t dly;
                for (dly = 0; dly < 200000; dly++);
            }
            w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);
            W25QXX_LOG("SR1 after clear = 0x%02X\n", sr);
        }
    }
    
    dev->initialized = true;
    
    W25QXX_LOG("Init OK: %s - MfgID=0x%02X, MemType=0x%02X, DevID=0x%02X, Size=%dMB\n",
               dev->name, id[0], id[1], id[2], total_size / (1024*1024));
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_DeInit(FlashDevice_t *dev)
{
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    dev->initialized = false;
    W25QXX_LOG("DeInit: %s\n", dev->name);
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
    FlashStatus_t ret;
    
    if (!dev || !buf) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    if (addr + len > dev->info.total_size) {
        return FLASH_ERR_PARAM;
    }
    
    /* 等待就绪 */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }
    
    /* 使用快速读取命令 — 合并 cmd+addr+dummy 为单次 DMA burst，
     * 消除逐字节发送时 SPIM DMA disable/re-enable 间隙导致的时序问题 */
    dev->cs.select();
    {
        uint8_t cmd_buf[5];
        cmd_buf[0] = W25QXX_CMD_FAST_READ;
        cmd_buf[1] = (addr >> 16) & 0xFF;
        cmd_buf[2] = (addr >> 8) & 0xFF;
        cmd_buf[3] = addr & 0xFF;
        cmd_buf[4] = 0xFF;  /* Dummy byte */
        spi_write(cmd_buf, 5);
    }
    
    /* 使用DMA批量读取 */
    spi_read(buf, (uint16_t)len);
    
    dev->cs.deselect();
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    uint32_t page_offset;
    uint32_t page_remain;
    uint32_t write_len;
    const uint8_t *p = buf;
    FlashStatus_t ret;
    
    if (!dev || !buf) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    if (addr + len > dev->info.total_size) {
        return FLASH_ERR_PARAM;
    }
    
    while (len > 0) {
        /* 计算当前页内偏移和剩余空间 */
        page_offset = addr & (W25QXX_PAGE_SIZE - 1);
        page_remain = W25QXX_PAGE_SIZE - page_offset;
        write_len = (len < page_remain) ? len : page_remain;
        
        /* 等待就绪 */
        ret = W25Qxx_WaitReady(dev, 100);
        if (ret != FLASH_OK) {
            return ret;
        }
        
        /* 写使能 + 验证WEL */
        ret = w25qxx_write_enable_verify(dev);
        if (ret != FLASH_OK) {
            W25QXX_LOG("Write: WREN failed at addr=0x%06X\n", (unsigned)addr);
            return FLASH_ERR_WRITE;
        }
        
        /* 页编程 — 合并 cmd+addr 为单次 DMA burst */
        dev->cs.select();
        {
            uint8_t cmd_buf[4];
            cmd_buf[0] = W25QXX_CMD_PAGE_PROGRAM;
            cmd_buf[1] = (addr >> 16) & 0xFF;
            cmd_buf[2] = (addr >> 8) & 0xFF;
            cmd_buf[3] = addr & 0xFF;
            spi_write(cmd_buf, 4);
        }
        
        /* 使用DMA批量写入 */
        spi_write((uint8_t*)p, (uint16_t)write_len);
        
        dev->cs.deselect();
        
        /* 等待写入完成 */
        ret = W25Qxx_WaitReady(dev, W25QXX_TIMEOUT_WRITE_PAGE);
        if (ret != FLASH_OK) {
            return FLASH_ERR_WRITE;
        }
        
        addr += write_len;
        p    += write_len;
        len  -= write_len;
    }
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_EraseSector(FlashDevice_t *dev, uint32_t addr)
{
    FlashStatus_t ret;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    /* 地址对齐到扇区边界 */
    addr &= ~(W25QXX_SECTOR_SIZE - 1);
    
    if (addr >= dev->info.total_size) {
        return FLASH_ERR_PARAM;
    }
    
    /* 等待就绪 */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }
    
    /* 写使能 + 验证WEL */
    ret = w25qxx_write_enable_verify(dev);
    if (ret != FLASH_OK) {
        W25QXX_LOG("EraseSector: WREN failed at addr=0x%06X\n", (unsigned)addr);
        return FLASH_ERR_ERASE;
    }
    
    /* 扇区擦除 — 合并 cmd+addr 为单次 DMA burst */
    dev->cs.select();
    {
        uint8_t cmd_buf[4];
        cmd_buf[0] = W25QXX_CMD_SECTOR_ERASE;
        cmd_buf[1] = (addr >> 16) & 0xFF;
        cmd_buf[2] = (addr >> 8) & 0xFF;
        cmd_buf[3] = addr & 0xFF;
        spi_write(cmd_buf, 4);
    }
    dev->cs.deselect();
    
    /* 等待擦除完成 */
    ret = W25Qxx_WaitReady(dev, W25QXX_TIMEOUT_ERASE_SECTOR);
    if (ret != FLASH_OK) {
        W25QXX_LOG("EraseSector: timeout at addr=0x%06X\n", (unsigned)addr);
        return FLASH_ERR_ERASE;
    }
    
    /* 擦除结果验证 (debug): 读回第一个字节确认为 0xFF */
    {
        uint8_t verify;
        uint8_t vcmd[5];
        vcmd[0] = W25QXX_CMD_FAST_READ;
        vcmd[1] = (addr >> 16) & 0xFF;
        vcmd[2] = (addr >> 8) & 0xFF;
        vcmd[3] = addr & 0xFF;
        vcmd[4] = 0xFF;
        dev->cs.select();
        spi_write(vcmd, 5);
        verify = spi_read_byte();
        dev->cs.deselect();
        if (verify != 0xFF) {
            W25QXX_LOG("EraseSector VERIFY FAIL: addr=0x%06X read=0x%02X (expected 0xFF)\n",
                       (unsigned)addr, verify);
        }
    }
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_EraseBlock(FlashDevice_t *dev, uint32_t addr)
{
    FlashStatus_t ret;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    /* 地址对齐到块边界 */
    addr &= ~(W25QXX_BLOCK_SIZE_64K - 1);
    
    if (addr >= dev->info.total_size) {
        return FLASH_ERR_PARAM;
    }
    
    /* 等待就绪 */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }
    
    /* 写使能 */
    w25qxx_write_enable(dev);
    
    /* 块擦除 — 合并 cmd+addr 为单次 DMA burst */
    dev->cs.select();
    {
        uint8_t cmd_buf[4];
        cmd_buf[0] = W25QXX_CMD_BLOCK_ERASE_64K;
        cmd_buf[1] = (addr >> 16) & 0xFF;
        cmd_buf[2] = (addr >> 8) & 0xFF;
        cmd_buf[3] = addr & 0xFF;
        spi_write(cmd_buf, 4);
    }
    dev->cs.deselect();
    
    /* 等待擦除完成 */
    ret = W25Qxx_WaitReady(dev, W25QXX_TIMEOUT_ERASE_BLOCK);
    if (ret != FLASH_OK) {
        return FLASH_ERR_ERASE;
    }
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_EraseChip(FlashDevice_t *dev)
{
    FlashStatus_t ret;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    /* 等待就绪 */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }
    
    /* 写使能 */
    w25qxx_write_enable(dev);
    
    /* 全片擦除 */
    w25qxx_send_cmd(dev, W25QXX_CMD_CHIP_ERASE);
    
    W25QXX_LOG("Chip erase started (may take up to 100 seconds)...\n");
    
    /* 等待擦除完成 */
    ret = W25Qxx_WaitReady(dev, W25QXX_TIMEOUT_ERASE_CHIP);
    if (ret != FLASH_OK) {
        return FLASH_ERR_ERASE;
    }
    
    W25QXX_LOG("Chip erase completed\n");
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_GetStatus(FlashDevice_t *dev, uint8_t *status)
{
    uint8_t sr;
    
    if (!dev || !status) {
        return FLASH_ERR_PARAM;
    }
    
    w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);
    *status = sr;
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_WaitReady(FlashDevice_t *dev, uint32_t timeout_ms)
{
    uint8_t sr;
    int rtos_running;
    uint32_t start_tick;
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    rtos_running = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
    start_tick = rtos_running ? xTaskGetTickCount() : 0;
    
    do {
        w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);
        
        if (!(sr & W25QXX_SR1_BUSY)) {
            return FLASH_OK;
        }
        
        if (rtos_running) {
            if (timeout_ms > 10) {
                vTaskDelay(1);
            }
        } else {
            volatile uint32_t dly;
            for (dly = 0; dly < 10000; dly++);
            start_tick++;  /* approximate 1ms per iteration */
        }
        
    } while (rtos_running
             ? ((xTaskGetTickCount() - start_tick) < (timeout_ms / portTICK_PERIOD_MS + 1))
             : (start_tick < timeout_ms));
    
    W25QXX_LOG("Wait ready timeout!\n");
    return FLASH_ERR_TIMEOUT;
}

static FlashStatus_t W25Qxx_ReadID(FlashDevice_t *dev)
{
    uint8_t id[3];
    
    if (!dev) {
        return FLASH_ERR_PARAM;
    }
    
    /* 读取JEDEC ID */
    w25qxx_cmd_read(dev, W25QXX_CMD_READ_JEDEC_ID, id, 3);
    
    dev->info.mfg_id   = id[0];
    dev->info.mem_type = id[1];
    dev->info.dev_id   = id[2];
    
    W25QXX_LOG("ReadID: MfgID=0x%02X, MemType=0x%02X, DevID=0x%02X\n",
               id[0], id[1], id[2]);
    
    return FLASH_OK;
}

static FlashStatus_t W25Qxx_GetInfo(FlashDevice_t *dev, FlashDevInfo_t *info)
{
    if (!dev || !info) {
        return FLASH_ERR_PARAM;
    }
    
    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    
    /* 复制设备信息 */
    memcpy(info, &dev->info, sizeof(FlashDevInfo_t));
    
    return FLASH_OK;
}

/*===========================================================================
 * 设备创建/销毁
 *===========================================================================*/

FlashDevice_t* W25Qxx_Create(const char *name,
                             void (*cs_select)(void),
                             void (*cs_deselect)(void),
                             void (*cs_init)(void))
{
    FlashDevice_t *dev;
    
    if (!name || !cs_select || !cs_deselect) {
        return NULL;
    }
    
    dev = (FlashDevice_t*)pvPortMalloc(sizeof(FlashDevice_t));
    if (!dev) {
        return NULL;
    }
    
    memset(dev, 0, sizeof(FlashDevice_t));
    
    /* 设置名称 */
    strncpy(dev->name, name, FLASH_DEV_NAME_MAX - 1);
    
    /* 设置类型 */
    dev->type = FLASH_TYPE_NOR;
    
    /* 设置操作表 */
    dev->ops = &g_w25qxx_ops;
    
    /* 设置CS控制 */
    dev->cs.select   = cs_select;
    dev->cs.deselect = cs_deselect;
    dev->cs.init     = cs_init;
    
    W25QXX_LOG("Created device: %s\n", name);
    
    return dev;
}

void W25Qxx_Destroy(FlashDevice_t *dev)
{
    if (!dev) return;
    
    if (dev->initialized && dev->ops && dev->ops->deinit) {
        dev->ops->deinit(dev);
    }
    
    W25QXX_LOG("Destroyed device: %s\n", dev->name);
    vPortFree(dev);
}

/*===========================================================================
 * 非阻塞全片擦除 API（供 flash_devices.c 的异步接口调用）
 *===========================================================================*/

/**
 * @brief 发送全片擦除命令后立即返回，不等待擦除完成
 *
 * 调用方须随后轮询 W25Qxx_IsBusy() 确认擦除结束，
 * 在擦除完成前不得对 Flash 进行任何写/擦除操作。
 *
 * @param dev  FlashDevice_t 设备指针
 * @return FLASH_OK        命令已发送
 *         FLASH_ERR_PARAM 参数非法
 *         FLASH_ERR_NOT_INIT 设备未初始化
 *         FLASH_ERR_TIMEOUT 发送前等待就绪超时（芯片异常）
 */
FlashStatus_t W25Qxx_EraseBlockStart(FlashDevice_t *dev, uint32_t addr)
{
    FlashStatus_t ret;

    if (!dev) {
        return FLASH_ERR_PARAM;
    }

    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }

    /* 地址对齐到 64KB 块边界 */
    addr &= ~(W25QXX_BLOCK_SIZE_64K - 1);

    if (addr >= dev->info.total_size) {
        return FLASH_ERR_PARAM;
    }

    /* 等待前序操作完成 */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }

    /* 写使能 */
    w25qxx_write_enable(dev);

    /* 发送块擦除命令，不等待完成 */
    dev->cs.select();
    w25qxx_cmd_addr(dev, W25QXX_CMD_BLOCK_ERASE_64K, addr);
    dev->cs.deselect();

    W25QXX_LOG("Block erase command sent at 0x%lX (async)\n", (unsigned long)addr);

    return FLASH_OK;
}

FlashStatus_t W25Qxx_EraseChipStart(FlashDevice_t *dev)
{
    FlashStatus_t ret;

    if (!dev) {
        return FLASH_ERR_PARAM;
    }

    if (!dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }

    /* 等待前序操作完成（最多 100ms，正常情况下芯片应已就绪） */
    ret = W25Qxx_WaitReady(dev, 100);
    if (ret != FLASH_OK) {
        return ret;
    }

    /* 写使能 */
    w25qxx_write_enable(dev);

    /* 发送全片擦除命令，不等待完成 */
    w25qxx_send_cmd(dev, W25QXX_CMD_CHIP_ERASE);

    W25QXX_LOG("Chip erase command sent (async, will poll busy)\n");

    return FLASH_OK;
}

/**
 * @brief 非阻塞忙状态查询 —— 读 SR1 BUSY 位
 *
 * @param dev  FlashDevice_t 设备指针
 * @return 1 = 仍在擦除（BUSY=1）；0 = 擦除完成或设备异常返回就绪
 */
uint8_t W25Qxx_IsBusy(FlashDevice_t *dev)
{
    uint8_t sr = 0;

    if (!dev || !dev->initialized) {
        return 0;  /* 设备无效，视为不忙（安全降级） */
    }

    w25qxx_cmd_read(dev, W25QXX_CMD_READ_STATUS_REG1, &sr, 1);

    return (sr & W25QXX_SR1_BUSY) ? 1 : 0;
}
