/**
 ******************************************************************************
 * @file    drv_device.c
 * @brief   驱动设备注册框架实现
 ******************************************************************************
 */
#include <string.h>
#include "drv_device.h"

#if DRV_DEVICE_EN

static DrvDevice_t *g_Devices[DRV_DEVICE_MAX];
static uint8_t      g_DeviceCount = 0;
static bool         g_Initialized = FALSE;

static const char *g_BusNames[] = {
    "spi", "i2c", "i2s", "sdio", "gpio", "uart", "power", "usb",
};

/*===========================================================================
 * 内部函数
 *===========================================================================*/

static int CreateDeviceParams(DrvDevice_t *dev, FsNode_t *devNode)
{
    const FsParamDef_t *param;
    FsNode_t *paramNode;
    int count = 0;

    if (!dev || !devNode || !dev->params) return 0;

    param = dev->params;
    while (param->name != NULL) {
        paramNode = DrvFs_CreateParam(devNode, param->name, param->desc,
                                       param->get, param->set, dev->privData);
        if (!paramNode) return -1;
        count++;
        param++;
    }
    return 0;
}

/*===========================================================================
 * 公共API实现
 *===========================================================================*/

int DrvDevice_Init(void)
{
    FsError_t err;
    if (g_Initialized) return 0;

    memset(g_Devices, 0, sizeof(g_Devices));
    g_DeviceCount = 0;

    err = DrvFs_Init();
    if (err != FS_OK) return -1;

    g_Initialized = TRUE;
    return 0;
}

int DrvDevice_Register(DrvDevice_t *dev)
{
    FsNode_t *busDir, *devNode;

    if (!dev || !dev->name) return -1;

    if (!g_Initialized) {
        if (DrvDevice_Init() != 0) return -1;
    }

    if (g_DeviceCount >= DRV_DEVICE_MAX) return -2;
    if (dev->isRegistered) return -3;

    busDir = DrvDevice_GetBusDir(dev->bus);
    if (!busDir) busDir = DrvFs_GetDriverDir();

    devNode = DrvFs_CreateDevice(busDir, dev->name, dev);
    if (!devNode) return -4;

    dev->fsNode = devNode;
    devNode->driver = dev;

    if (dev->params) {
        if (CreateDeviceParams(dev, devNode) != 0) {
            DrvFs_RemoveNode(devNode);
            dev->fsNode = NULL;
            return -5;
        }
    }

    g_Devices[g_DeviceCount++] = dev;
    dev->isRegistered = TRUE;

    if (dev->init) {
        dev->init(dev->privData);
    }
    return 0;
}

int DrvDevice_Unregister(DrvDevice_t *dev)
{
    uint8_t i, j;
    if (!dev || !dev->isRegistered) return -1;

    if (dev->deinit) dev->deinit(dev->privData);
    if (dev->fsNode) { DrvFs_RemoveNode(dev->fsNode); dev->fsNode = NULL; }

    for (i = 0; i < g_DeviceCount; i++) {
        if (g_Devices[i] == dev) {
            for (j = i; j < g_DeviceCount - 1; j++)
                g_Devices[j] = g_Devices[j + 1];
            g_Devices[g_DeviceCount - 1] = NULL;
            g_DeviceCount--;
            break;
        }
    }

    dev->isRegistered = FALSE;
    dev->isOpened = FALSE;
    return 0;
}

DrvDevice_t* DrvDevice_Find(const char *name)
{
    uint8_t i;
    if (!name) return NULL;
    for (i = 0; i < g_DeviceCount; i++) {
        if (g_Devices[i] && strcmp(g_Devices[i]->name, name) == 0)
            return g_Devices[i];
    }
    return NULL;
}

DrvDevice_t* DrvDevice_FindByPath(const char *path)
{
    FsNode_t *node;
    if (!path) return NULL;
    node = DrvFs_FindNode(path);
    if (!node || node->type != FS_NODE_DEV) return NULL;
    return (DrvDevice_t*)node->driver;
}

FsNode_t* DrvDevice_GetBusDir(DrvBusType_t bus)
{
    switch (bus) {
        case DRV_BUS_SPI:   return DrvFs_GetSpiDir();
        case DRV_BUS_I2C:   return DrvFs_GetI2cDir();
        case DRV_BUS_I2S:   return DrvFs_GetI2sDir();
        case DRV_BUS_SDIO:  return DrvFs_GetSdioDir();
        case DRV_BUS_POWER: return DrvFs_GetPowerDir();
        case DRV_BUS_USB:   return DrvFs_GetUsbDir();
        default:            return DrvFs_GetDriverDir();
    }
}

const char* DrvDevice_GetBusName(DrvBusType_t bus)
{
    if (bus < DRV_BUS_MAX) return g_BusNames[bus];
    return "unknown";
}

void DrvDevice_List(DrvDeviceListCallback_t callback, void *userData)
{
    uint8_t i;
    if (!callback) return;
    for (i = 0; i < g_DeviceCount; i++) {
        if (g_Devices[i]) callback(g_Devices[i], userData);
    }
}

int DrvDevice_GetCount(void) { return g_DeviceCount; }

DrvDevice_t** DrvDevice_GetList(int *count)
{
    if (count) *count = g_DeviceCount;
    return g_Devices;
}

#endif /* DRV_DEVICE_EN */
