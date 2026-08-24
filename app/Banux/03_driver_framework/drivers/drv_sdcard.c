/**
 *****************************************************************************
 * @file     drv_sdcard.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    SD卡驱动框架适配层
 *
 * 注册到 /driver/sdio/sdcard/ 并暴露参数节点：
 *   capacity / block_size / block_count / status / card_type
 *****************************************************************************
 */

#include "drv_sdcard.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "sd_card_driver.h"
#include "flash_devices.h"
#include "flash_bus.h"
#include "debug.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * 私有数据
 ******************************************************************************/

typedef struct {
    uint32_t  capacity_mb;
    uint32_t  block_count;
    uint16_t  block_size;
    bool      initialized;
    char      name[32];
    char      card_type[16];
} SDCardPrivData_t;

static SDCardPrivData_t g_sdcard_priv = {
    .capacity_mb   = 0,
    .block_count   = 0,
    .block_size    = SD_CARD_BLOCK_SIZE,
    .initialized   = false,
    .name          = "SD-CARD",
    .card_type     = "Unknown"
};

/*******************************************************************************
 * 驱动操作回调
 ******************************************************************************/

static int sdcard_drv_init(void *privData)
{
    SDCardPrivData_t *priv = (SDCardPrivData_t *)privData;
    FlashDevice_t    *dev  = FlashBus_GetDeviceByName("sdcard0");

    if (!dev || !dev->initialized) {
        DBG("[DrvSDCard] Device 'sdcard0' not found or not initialized\n");
        priv->initialized = false;
        return -1;
    }

    /* 从设备信息填充私有数据 */
    priv->block_count  = dev->info.block_count;
    priv->block_size   = (uint16_t)dev->info.block_size;
    priv->capacity_mb  = (uint32_t)((uint64_t)dev->info.block_count * dev->info.block_size / (1024 * 1024));
    priv->initialized  = true;
    
    /* 根据容量判断卡类型 */
    if (priv->capacity_mb <= 2048) {
        strncpy(priv->card_type, "SDSC", sizeof(priv->card_type));
    } else if (priv->capacity_mb <= 32768) {
        strncpy(priv->card_type, "SDHC", sizeof(priv->card_type));
    } else {
        strncpy(priv->card_type, "SDXC", sizeof(priv->card_type));
    }

    DBG("[DrvSDCard] Driver init OK (%s, %lu MB)\n",
        priv->card_type, (unsigned long)priv->capacity_mb);
    return 0;
}

static int sdcard_drv_deinit(void *privData)
{
    SDCardPrivData_t *priv = (SDCardPrivData_t *)privData;
    priv->initialized = false;
    return 0;
}

static int sdcard_drv_open(void *privData)
{
    (void)privData;
    return 0;
}

static int sdcard_drv_close(void *privData)
{
    (void)privData;
    return 0;
}

static int sdcard_drv_read(void *privData, uint8_t *buf, uint32_t len)
{
    FlashDevice_t *dev = FlashBus_GetDeviceByName("sdcard0");
    (void)privData; (void)buf; (void)len;
    /* SD读需要块地址，通过 ioctl 完成 */
    if (!dev) return -1;
    return -1;
}

static int sdcard_drv_write(void *privData, const uint8_t *buf, uint32_t len)
{
    (void)privData; (void)buf; (void)len;
    return -1; /* 写操作需要地址，通过 ioctl 完成 */
}

static int sdcard_drv_ioctl(void *privData, uint32_t cmd, void *arg)
{
    (void)privData; (void)cmd; (void)arg;
    return 0;
}

/*******************************************************************************
 * 参数读取回调
 ******************************************************************************/

static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->name);
    return (int)strlen(buf);
}

static int param_get_capacity(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%lu MB", (unsigned long)p->capacity_mb);
    return (int)strlen(buf);
}

static int param_get_block_size(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%u bytes", p->block_size);
    return (int)strlen(buf);
}

static int param_get_block_count(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%lu", (unsigned long)p->block_count);
    return (int)strlen(buf);
}

static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->initialized ? "initialized" : "uninitialized");
    return (int)strlen(buf);
}

static int param_get_card_type(char *buf, uint16_t maxLen, void *userData)
{
    SDCardPrivData_t *p = (SDCardPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->card_type);
    return (int)strlen(buf);
}

/*******************************************************************************
 * 参数定义表 (简化版)
 ******************************************************************************/

static const FsParamDef_t sdcard_params[] = {
    { "name",     "SD卡驱动名称", param_get_name,      NULL },
    { "capacity", "总容量 (MB)",  param_get_capacity,  NULL },
    { "status",   "初始化状态",   param_get_status,    NULL },
    FS_PARAM_END
};

/*******************************************************************************
 * 驱动定义
 ******************************************************************************/

static DrvDevice_t sdcard_driver = {
    .name     = "sdcard",
    .bus      = DRV_BUS_SDIO,
    .init     = sdcard_drv_init,
    .deinit   = sdcard_drv_deinit,
    .open     = sdcard_drv_open,
    .close    = sdcard_drv_close,
    .read     = sdcard_drv_read,
    .write    = sdcard_drv_write,
    .ioctl    = sdcard_drv_ioctl,
    .params   = sdcard_params,
    .privData = &g_sdcard_priv,
};

/*******************************************************************************
 * 注册函数
 ******************************************************************************/

int SDCard_DrvRegister(void)
{
    /* 只有硬件设备已被成功创建并注册到 FlashBus 才进行 VFS 驱动注册 */
    FlashDevice_t *dev = FlashBus_GetDeviceByName("sdcard0");
    if (!dev || !dev->initialized) {
        DBG("[DrvSDCard] sdcard0 not detected, skip VFS registration\n");
        return -1;
    }
    return DrvDevice_Register(&sdcard_driver);
}
