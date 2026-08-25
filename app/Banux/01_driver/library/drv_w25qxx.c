/**
 *****************************************************************************
 * @file     drv_w25qxx.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    W25Qxx Flash驱动框架适配层
 *****************************************************************************
 * @attention
 *
 * 将W25Qxx Flash驱动注册到驱动框架，提供：
 * 1. 驱动注册到/driver/spi/w25qxx
 * 2. 参数节点：capacity/page_size/sector_size等
 * 3. Shell命令访问: cat /driver/spi/w25qxx/capacity
 *
 *****************************************************************************
 */

#include "drv_w25qxx.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "flash_nor_w25qxx.h"
#include "flash_devices.h"
#include "flash_bus.h"
#include "BG_FlashMgr.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * 私有数据结构
 ******************************************************************************/
typedef struct {
    uint32_t capacity;      // Flash容量(字节)
    uint16_t page_size;     // 页大小
    uint32_t sector_size;   // 扇区大小
    bool initialized;
    char name[32];
    uint16_t device_id;
} W25qxxPrivData_t;

static W25qxxPrivData_t g_w25qxx_priv = {
    .capacity = 0,
    .page_size = 256,
    .sector_size = 4096,
    .initialized = false,
    .name = "W25Qxx_Flash",
    .device_id = 0
};

static FlashDevice_t *w25qxx_find_present_device(void)
{
    FlashDevice_t *dev;

    dev = FlashDevices_GetSystemFlash();
    if (dev && dev->initialized && dev->type == FLASH_TYPE_NOR) {
        return dev;
    }

    dev = FlashDevices_GetStorageFlash();
    if (dev && dev->initialized && dev->type == FLASH_TYPE_NOR) {
        return dev;
    }

    dev = FlashBus_GetDeviceByName("flash0_sys");
    if (dev && dev->initialized && dev->type == FLASH_TYPE_NOR) {
        return dev;
    }

    dev = FlashBus_GetDeviceByName("flash1_stor");
    if (dev && dev->initialized && dev->type == FLASH_TYPE_NOR) {
        return dev;
    }

    return NULL;
}

/*******************************************************************************
 * 参数读写回调函数
 ******************************************************************************/

static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", priv->name);
    return strlen(buf);
}

static int param_get_capacity(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    // 显示为KB
    snprintf(buf, maxLen, "%lu KB", (unsigned long)(priv->capacity / 1024));
    return strlen(buf);
}

static int param_get_page_size(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    snprintf(buf, maxLen, "%u", priv->page_size);
    return strlen(buf);
}

static int param_get_sector_size(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    snprintf(buf, maxLen, "%lu", (unsigned long)priv->sector_size);
    return strlen(buf);
}

static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", priv->initialized ? "initialized" : "uninitialized");
    return strlen(buf);
}

static int param_get_device_id(char *buf, uint16_t maxLen, void *userData)
{
    W25qxxPrivData_t *priv = (W25qxxPrivData_t *)userData;
    snprintf(buf, maxLen, "0x%04X", priv->device_id);
    return strlen(buf);
}

static int param_cmd_erase_chip(const char *value, void *userData)
{
    if (strcmp(value, "confirm") == 0) {
        // TODO: 执行全片擦除
        // Flash_EraseChip();
        return 0;
    }
    return -1;
}

/*******************************************************************************
 * 参数定义表
 ******************************************************************************/
static const FsParamDef_t w25qxx_params[] = {
    {
        .name = "name",
        .desc = "Flash驱动名称",
        .get = param_get_name,
        .set = NULL,
    },
    {
        .name = "capacity",
        .desc = "Flash容量",
        .get = param_get_capacity,
        .set = NULL,
    },
    {
        .name = "page_size",
        .desc = "页大小(字节)",
        .get = param_get_page_size,
        .set = NULL,
    },
    {
        .name = "sector_size",
        .desc = "扇区大小(字节)",
        .get = param_get_sector_size,
        .set = NULL,
    },
    {
        .name = "status",
        .desc = "初始化状态",
        .get = param_get_status,
        .set = NULL,
    },
    {
        .name = "device_id",
        .desc = "设备ID",
        .get = param_get_device_id,
        .set = NULL,
    },
    {
        .name = "erase_chip",
        .desc = "全片擦除(写入'confirm'执行)",
        .get = NULL,
        .set = param_cmd_erase_chip,
    },
    FS_PARAM_END
};

/*******************************************************************************
 * 驱动操作函数
 ******************************************************************************/

static int w25qxx_drv_init(void *priv)
{
    W25qxxPrivData_t *flash = (W25qxxPrivData_t *)priv;
    FlashDevice_t *dev;
    
    if (flash->initialized) {
        return 0;
    }

    dev = w25qxx_find_present_device();
    if (!dev) {
        DBG("[DrvW25Qxx] No initialized W25Qxx device found\n");
        return -1;
    }

    flash->device_id = ((uint16_t)dev->info.mfg_id << 8) | dev->info.dev_id;
    flash->capacity = dev->info.total_size;
    flash->page_size = (uint16_t)dev->info.page_size;
    flash->sector_size = dev->info.sector_size;
    
    flash->initialized = true;
    
    return 0;
}

static int w25qxx_drv_deinit(void *priv)
{
    W25qxxPrivData_t *flash = (W25qxxPrivData_t *)priv;
    flash->initialized = false;
    return 0;
}

static int w25qxx_drv_open(void *priv)
{
    return 0;
}

static int w25qxx_drv_close(void *priv)
{
    return 0;
}

static int w25qxx_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    // 实现Flash读取
    // return Flash_Read(0, buf, len);
    return len;
}

static int w25qxx_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    // 实现Flash写入
    // return Flash_Write(0, buf, len);
    return len;
}

static int w25qxx_drv_ioctl(void *priv, uint32_t cmd, void *arg)
{
    W25qxxPrivData_t *flash = (W25qxxPrivData_t *)priv;
    
    switch (cmd) {
        case 0x01:  // 擦除扇区
        {
            uint32_t addr = *(uint32_t *)arg;
            // Flash_EraseSector(addr);
            break;
        }
        case 0x02:  // 擦除块
        {
            uint32_t addr = *(uint32_t *)arg;
            // Flash_EraseBlock(addr);
            break;
        }
        case 0x03:  // 全片擦除
            // Flash_EraseChip();
            break;
        default:
            return -1;
    }
    
    return 0;
}

/*******************************************************************************
 * 驱动定义
 ******************************************************************************/
/* 注意：不能用const，因为需要在运行时修改isRegistered/fsNode等字段 */
static DrvDevice_t w25qxx_driver = {
    .name = "w25qxx",
    .bus = DRV_BUS_SPI,
    .init = w25qxx_drv_init,
    .deinit = w25qxx_drv_deinit,
    .open = w25qxx_drv_open,
    .close = w25qxx_drv_close,
    .read = w25qxx_drv_read,
    .write = w25qxx_drv_write,
    .ioctl = w25qxx_drv_ioctl,
    .params = w25qxx_params,
    .privData = &g_w25qxx_priv,
};

/*******************************************************************************
 * 驱动注册函数
 ******************************************************************************/
int W25qxx_DrvRegister(void)
{
    if (!w25qxx_find_present_device()) {
        DBG("[DrvW25Qxx] W25Qxx not detected, skip VFS registration\n");
        return -1;
    }

    return DrvDevice_Register(&w25qxx_driver);
}
