/**
 *****************************************************************************
 * @file     drv_psram.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     03-April-2026
 * @brief    ESP-PSRAM64H 驱动框架适配层
 *
 * 注册到 /driver/spi/psram/ 并暴露参数节点：
 *   capacity / page_size / status / device_id
 *****************************************************************************
 */

#include "drv_psram.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "psram_esp64h.h"
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
    bool      initialized;
    char      name[32];
    uint8_t   mfg_id;
    uint8_t   kgd;
} PsramPrivData_t;

static PsramPrivData_t g_psram_priv = {
    .capacity_mb   = PSRAM64H_TOTAL_SIZE / (1024u * 1024u),
    .page_size     = PSRAM64H_PAGE_SIZE,
    .initialized   = false,
    .name          = "ESP-PSRAM64H",
    .mfg_id        = 0,
    .kgd           = 0
};

/*******************************************************************************
 * 驱动操作回调
 ******************************************************************************/

static int psram_drv_init(void *privData)
{
    PsramPrivData_t *priv = (PsramPrivData_t *)privData;
    FlashDevice_t   *dev  = FlashBus_GetDeviceByName("psram0");

    if (!dev || !dev->initialized) {
        DBG("[DrvPSRAM] Device 'psram0' not found or not initialized\n");
        priv->initialized = false;
        return -1;
    }

    /* 从设备信息填充私有数据 */
    priv->mfg_id      = dev->info.mfg_id;
    priv->kgd         = dev->info.dev_id;
    priv->page_size   = (uint16_t)dev->info.page_size;
    priv->initialized = true;

    DBG("[DrvPSRAM] Driver init OK (ID: 0x%02X 0x%02X)\n",
        priv->mfg_id, priv->kgd);
    return 0;
}

static int psram_drv_deinit(void *privData)
{
    PsramPrivData_t *priv = (PsramPrivData_t *)privData;
    priv->initialized = false;
    return 0;
}

static int psram_drv_open(void *privData)
{
    (void)privData;
    return 0;
}

static int psram_drv_close(void *privData)
{
    (void)privData;
    return 0;
}

static int psram_drv_read(void *privData, uint8_t *buf, uint32_t len)
{
    (void)privData; (void)buf; (void)len;
    return 0; /* 参数节点读取，无需此函数 */
}

static int psram_drv_write(void *privData, const uint8_t *buf, uint32_t len)
{
    (void)privData; (void)buf; (void)len;
    return 0; /* 参数节点写入，无需此函数 */
}

static int psram_drv_ioctl(void *privData, uint32_t cmd, void *arg)
{
    (void)privData; (void)cmd; (void)arg;
    return 0;
}

/*******************************************************************************
 * 参数读取回调
 ******************************************************************************/

static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    PsramPrivData_t *p = (PsramPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->name);
    return (int)strlen(buf);
}

static int param_get_capacity(char *buf, uint16_t maxLen, void *userData)
{
    PsramPrivData_t *p = (PsramPrivData_t *)userData;
    snprintf(buf, maxLen, "%lu MB", (unsigned long)p->capacity_mb);
    return (int)strlen(buf);
}

static int param_get_page_size(char *buf, uint16_t maxLen, void *userData)
{
    PsramPrivData_t *p = (PsramPrivData_t *)userData;
    snprintf(buf, maxLen, "%u bytes", p->page_size);
    return (int)strlen(buf);
}

static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    PsramPrivData_t *p = (PsramPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", p->initialized ? "initialized" : "uninitialized");
    return (int)strlen(buf);
}

static int param_get_device_id(char *buf, uint16_t maxLen, void *userData)
{
    PsramPrivData_t *p = (PsramPrivData_t *)userData;
    snprintf(buf, maxLen, "0x%02X%02X (Mfg=0x%02X KGD=0x%02X)",
             p->mfg_id, p->kgd,
             p->mfg_id, p->kgd);
    return (int)strlen(buf);
}

/*******************************************************************************
 * 参数定义表 (简化版)
 ******************************************************************************/

static const FsParamDef_t psram_params[] = {
    { "name",     "PSRAM 驱动名称", param_get_name,     NULL },
    { "capacity", "总容量 (MB)",    param_get_capacity, NULL },
    { "status",   "初始化状态",     param_get_status,   NULL },
    FS_PARAM_END
};

/*******************************************************************************
 * 驱动定义
 ******************************************************************************/

static DrvDevice_t psram_driver = {
    .name     = "psram",
    .bus      = DRV_BUS_SPI,
    .init     = psram_drv_init,
    .deinit   = psram_drv_deinit,
    .open     = psram_drv_open,
    .close    = psram_drv_close,
    .read     = psram_drv_read,
    .write    = psram_drv_write,
    .ioctl    = psram_drv_ioctl,
    .params   = psram_params,
    .privData = &g_psram_priv,
};

/*******************************************************************************
 * 注册函数
 ******************************************************************************/

int Psram_DrvRegister(void)
{
    /* 只有硬件设备已被成功创建并注册到 FlashBus 才进行 VFS 驱动注册 */
    FlashDevice_t *dev = FlashBus_GetDeviceByName("psram0");
    if (!dev || !dev->initialized) {
        DBG("[DrvPSRAM] psram0 not detected, skip VFS registration\n");
        return -1;
    }
    return DrvDevice_Register(&psram_driver);
}
