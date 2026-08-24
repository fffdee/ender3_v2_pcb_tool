/**
 *****************************************************************************
 * @file     drv_w25n02.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    W25N02 NAND Flash 驱动框架适配层
 *
 * 注册到 /driver/spi/w25n02/ 并暴露参数节点：
 *   capacity / page_size / block_size / block_count /
 *   bad_blocks / status / device_id / scan_bbt
 *****************************************************************************
 */

#include "drv_w25n02.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "flash_nand_w25n02.h"
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
    uint16_t  page_size;
    uint32_t  block_size_kb;
    uint16_t  block_count;
    uint16_t  bad_blocks;
    bool      initialized;
    char      name[32];
    uint8_t   mfg_id;
    uint8_t   mem_type;
    uint8_t   dev_id;
} W25n02PrivData_t;

static W25n02PrivData_t g_w25n02_priv = {
    .capacity_mb   = W25N02_TOTAL_SIZE / (1024u * 1024u),
    .page_size     = W25N02_PAGE_SIZE,
    .block_size_kb = W25N02_BLOCK_SIZE / 1024u,
    .block_count   = W25N02_BLOCK_COUNT,
    .bad_blocks    = 0,
    .initialized   = false,
    .name          = "W25N02_NAND",
    .mfg_id        = 0,
    .mem_type      = 0,
    .dev_id        = 0
};

/*******************************************************************************
 * 驱动操作回调
 ******************************************************************************/

static int w25n02_drv_init(void *privData)
{
    W25n02PrivData_t *priv = (W25n02PrivData_t *)privData;
    FlashDevice_t    *dev  = FlashBus_GetDeviceByName("nand0");

    if (!dev || !dev->initialized) {
        DBG("[DrvW25N02] Device 'nand0' not found or not initialized\n");
        priv->initialized = false;
        return -1;
    }

    /* 从设备信息填充私有数据 */
    priv->mfg_id      = dev->info.mfg_id;
    priv->mem_type    = dev->info.mem_type;
    priv->dev_id      = dev->info.dev_id;
    priv->page_size   = (uint16_t)dev->info.page_size;
    priv->block_count = (uint16_t)dev->info.block_count;
    priv->initialized = true;

    DBG("[DrvW25N02] Driver init OK (ID: %02X %02X %02X)\n",
        priv->mfg_id, priv->mem_type, priv->dev_id);
    return 0;
}

static int w25n02_drv_deinit(void *privData)
{
    W25n02PrivData_t *priv = (W25n02PrivData_t *)privData;
    priv->initialized = false;
    return 0;
}

static int w25n02_drv_open(void *privData)
{
    (void)privData;
    return 0;
}

static int w25n02_drv_close(void *privData)
{
    (void)privData;
    return 0;
}

static int w25n02_drv_read(void *privData, uint8_t *buf, uint32_t len)
{
    FlashDevice_t *dev = FlashBus_GetDeviceByName("nand0");
    (void)privData; (void)buf; (void)len;
    /* NAND读需要地址，通过 ioctl 完成 */
    if (!dev) return -1;
    return -1;
}

static int w25n02_drv_write(void *privData, const uint8_t *buf, uint32_t len)
{
    (void)privData; (void)buf; (void)len;
    return -1; /* 写操作需要地址，通过 ioctl 完成 */
}

static int w25n02_drv_ioctl(void *privData, uint32_t cmd, void *arg)
{
    (void)privData; (void)cmd; (void)arg;
    return 0;
}

/*******************************************************************************
 * 参数读取回调
 ******************************************************************************/

static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->name);
    return (int)strlen(buf);
}

static int param_get_capacity(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%lu MB", (unsigned long)p->capacity_mb);
    return (int)strlen(buf);
}

static int param_get_page_size(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%u bytes", p->page_size);
    return (int)strlen(buf);
}

static int param_get_block_size(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%lu KB", (unsigned long)p->block_size_kb);
    return (int)strlen(buf);
}

static int param_get_block_count(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%u", p->block_count);
    return (int)strlen(buf);
}

static int param_get_bad_blocks(char *buf, uint16_t maxLen, void *userData)
{
    FlashDevice_t       *dev = FlashBus_GetDeviceByName("nand0");
    const W25N02_BBM_t  *bbm = NULL;

    (void)userData;
    if (dev && W25N02_GetBBM(dev, &bbm) == FLASH_OK && bbm) {
        snprintf(buf, maxLen, "%u / %u", bbm->bad_count, W25N02_BLOCK_COUNT);
    } else {
        snprintf(buf, maxLen, "N/A");
    }
    return (int)strlen(buf);
}

static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->initialized ? "initialized" : "uninitialized");
    return (int)strlen(buf);
}

static int param_get_device_id(char *buf, uint16_t maxLen, void *userData)
{
    W25n02PrivData_t *p = (W25n02PrivData_t *)userData;
    snprintf(buf, maxLen, "0x%02X%02X%02X (Mfg=%02X Type=%02X Dev=%02X)",
             p->mfg_id, p->mem_type, p->dev_id,
             p->mfg_id, p->mem_type, p->dev_id);
    return (int)strlen(buf);
}

static int param_cmd_scan_bbt(const char *value, void *userData)
{
    (void)userData;
    if (strcmp(value, "start") == 0) {
        FlashDevice_t *dev = FlashBus_GetDeviceByName("nand0");
        if (!dev || !dev->initialized) {
            return -1;
        }
        return (W25N02_ScanBBT(dev) == FLASH_OK) ? 0 : -1;
    }
    return -1;
}

/*******************************************************************************
 * 参数定义表 (简化版，避免参数分配器溢出)
 ******************************************************************************/

static const FsParamDef_t w25n02_params[] = {
    { "name",     "NAND 驱动名称", param_get_name,  NULL },
    { "capacity", "总容量 (MB)",   param_get_capacity, NULL },
    { "status",   "初始化状态",    param_get_status, NULL },
    FS_PARAM_END
};

/*******************************************************************************
 * 驱动定义
 ******************************************************************************/

static DrvDevice_t w25n02_driver = {
    .name     = "w25n02",
    .bus      = DRV_BUS_SPI,
    .init     = w25n02_drv_init,
    .deinit   = w25n02_drv_deinit,
    .open     = w25n02_drv_open,
    .close    = w25n02_drv_close,
    .read     = w25n02_drv_read,
    .write    = w25n02_drv_write,
    .ioctl    = w25n02_drv_ioctl,
    .params   = w25n02_params,
    .privData = &g_w25n02_priv,
};

/*******************************************************************************
 * 注册函数
 ******************************************************************************/

int W25n02_DrvRegister(void)
{
    /* 只有硬件设备已被成功创建并注册到 FlashBus 才进行 VFS 驱动注册 */
    FlashDevice_t *dev = FlashBus_GetDeviceByName("nand0");
    if (!dev || !dev->initialized) {
        DBG("[DrvW25N02] nand0 not detected, skip VFS registration\n");
        return -1;
    }
    return DrvDevice_Register(&w25n02_driver);
}
