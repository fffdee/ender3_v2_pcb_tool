/**
 *****************************************************************************
 * @file     drv_device.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    驱动设备注册框架实现
 *****************************************************************************
 */

#include <string.h>
#include <stdio.h>
#include "drv_device.h"
#include "debug.h"  /* For DBG macro */

/*******************************************************************************
 * 静态变量
 ******************************************************************************/
static DrvDevice_t *g_Devices[DRV_DEVICE_MAX];
static uint8_t      g_DeviceCount = 0;
static bool         g_Initialized = FALSE;

/* 总线名称表 */
static const char *g_BusNames[] = {
    "spi",      /* DRV_BUS_SPI */
    "i2c",      /* DRV_BUS_I2C */
    "i2s",      /* DRV_BUS_I2S */
    "sdio",     /* DRV_BUS_SDIO */
    "gpio",     /* DRV_BUS_GPIO */
    "uart",     /* DRV_BUS_UART */
    "power",    /* DRV_BUS_POWER */
    "usb",      /* DRV_BUS_USB */
    "timer",    /* DRV_BUS_TIMER */
};

/*******************************************************************************
 * 内部函数
 ******************************************************************************/

/**
 * @brief  为设备创建参数节点
 */
static int CreateDeviceParams(DrvDevice_t *dev, FsNode_t *devNode)
{
    const FsParamDef_t *param;
    FsNode_t *paramNode;
    int count = 0;
    
    DBG("[CreateParams] Start: dev=%p, devNode=%p, params=%p\n", dev, devNode, dev ? dev->params : NULL);
    
    if (!dev || !devNode || !dev->params) {
        DBG("[CreateParams] Early return (no params)\n");
        return 0;
    }
    
    param = dev->params;
    DBG("[CreateParams] First param: %p\n", param);
    
    while (param->name != NULL) {
        DBG("[CreateParams] Param[%d]: name=%p\n", count, param->name);
        
        if (param->name) {
            DBG("[CreateParams] Param[%d] name='%s'\n", count, param->name);
        }
        
        DBG("[CreateParams] Calling DrvFs_CreateParam...\n");
        paramNode = DrvFs_CreateParam(
            devNode,
            param->name,
            param->desc,
            param->get,
            param->set,
            dev->privData  /* 传递设备私有数据 */
        );
        
        if (!paramNode) {
            DBG("[CreateParams] ERROR: DrvFs_CreateParam failed for '%s'\n", param->name);
            return -1;
        }
        
        DBG("[CreateParams] Param[%d] created: %p\n", count, paramNode);
        count++;
        param++;
    }
    
    DBG("[CreateParams] All %d params created\n", count);
    return 0;
}

/*******************************************************************************
 * 公共API实现
 ******************************************************************************/

int DrvDevice_Init(void)
{
    FsError_t err;
    
    if (g_Initialized) return 0;
    
    /* 初始化设备列表 */
    memset(g_Devices, 0, sizeof(g_Devices));
    g_DeviceCount = 0;
    
    /* 初始化文件系统 */
    err = DrvFs_Init();
    if (err != FS_OK) {
        return -1;
    }
    
    g_Initialized = TRUE;
    return 0;
}

int DrvDevice_Register(DrvDevice_t *dev)
{
    FsNode_t *busDir;
    FsNode_t *devNode;
    
    DBG("[DrvDev] Register start: dev=%p\n", dev);
    
    if (!dev || !dev->name) {
        DBG("[DrvDev] ERROR: Invalid device pointer or name\n");
        return -1;
    }
    
    DBG("[DrvDev] Device name: '%s'\n", dev->name);
    
    if (!g_Initialized) {
        DBG("[DrvDev] Not initialized, calling DrvDevice_Init()...\n");
        if (DrvDevice_Init() != 0) {
            DBG("[DrvDev] ERROR: DrvDevice_Init failed\n");
            return -1;
        }
    }
    
    if (g_DeviceCount >= DRV_DEVICE_MAX) {
        DBG("[DrvDev] ERROR: Device count limit reached\n");
        return -2;
    }
    
    if (dev->isRegistered) {
        DBG("[DrvDev] ERROR: Already registered\n");
        return -3;  /* 已注册 */
    }

    /* 先初始化驱动: 初始化成功后才挂载到 VFS 总线 */
    if (dev->init) {
        DBG("[DrvDev] Calling init for device '%s'...\n", dev->name);
        if (dev->init(dev->privData) != 0) {
            DBG("[DrvDev] ERROR: Device '%s' init failed, NOT registered\n", dev->name);
            return -6;  /* 初始化失败, 不注册 */
        }
        DBG("[DrvDev] Device '%s' initialized successfully\n", dev->name);
    }

    DBG("[DrvDev] Getting bus directory (bus=%d)...\n", dev->bus);
    /* 获取对应总线目录 (总线目录懒创建, 首次访问时创建) */
    busDir = DrvDevice_GetBusDir(dev->bus);
    if (!busDir) {
        DBG("[DrvDev] Bus dir not found, using driver dir\n");
        /* 如果是未知总线，在driver下创建 */
        busDir = DrvFs_GetDriverDir();
    }
    DBG("[DrvDev] Bus dir: %p\n", busDir);
    
    DBG("[DrvDev] Creating device node...\n");
    /* 创建设备节点 */
    devNode = DrvFs_CreateDevice(busDir, dev->name, dev);
    if (!devNode) {
        DBG("[DrvDev] ERROR: DrvFs_CreateDevice failed\n");
        return -4;
    }
    DBG("[DrvDev] Device node created: %p\n", devNode);
    
    /* 关联设备和节点 */
    dev->fsNode = devNode;
    devNode->driver = dev;
    
    DBG("[DrvDev] Creating params (params=%p)...\n", dev->params);
    /* 创建参数节点 */
    if (dev->params) {
        if (CreateDeviceParams(dev, devNode) != 0) {
            DBG("[DrvDev] ERROR: CreateDeviceParams failed\n");
            DrvFs_RemoveNode(devNode);
            dev->fsNode = NULL;
            return -5;
        }
        DBG("[DrvDev] Params created\n");
    }
    
    /* 添加到设备列表 */
    g_Devices[g_DeviceCount++] = dev;
    dev->isRegistered = TRUE;
    
    DBG("[DrvDev] Device '%s' registered successfully\n", dev->name);
    
    return 0;
}

int DrvDevice_Unregister(DrvDevice_t *dev)
{
    uint8_t i, j;
    
    if (!dev || !dev->isRegistered) return -1;
    
    /* 调用驱动去初始化 */
    if (dev->deinit) {
        dev->deinit(dev->privData);
    }
    
    /* 从文件系统中移除节点 */
    if (dev->fsNode) {
        DrvFs_RemoveNode(dev->fsNode);
        dev->fsNode = NULL;
    }
    
    /* 从设备列表中移除 */
    for (i = 0; i < g_DeviceCount; i++) {
        if (g_Devices[i] == dev) {
            for (j = i; j < g_DeviceCount - 1; j++) {
                g_Devices[j] = g_Devices[j + 1];
            }
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
        if (g_Devices[i] && strcmp(g_Devices[i]->name, name) == 0) {
            return g_Devices[i];
        }
    }
    
    return NULL;
}

DrvDevice_t* DrvDevice_FindByPath(const char *path)
{
    FsNode_t *node;
    
    if (!path) return NULL;
    
    node = DrvFs_FindNode(path);
    if (!node) return NULL;
    if (node->type != FS_NODE_DEV) return NULL;
    
    return (DrvDevice_t*)node->driver;
}

FsNode_t* DrvDevice_GetBusDir(DrvBusType_t bus)
{
    switch (bus) {
        case DRV_BUS_SPI:   return DrvFs_GetSpiDir();
        case DRV_BUS_I2C:   return DrvFs_GetI2cDir();
        case DRV_BUS_I2S:   return DrvFs_GetI2sDir();
        case DRV_BUS_SDIO:  return DrvFs_GetSdioDir();
        case DRV_BUS_GPIO:  return DrvFs_GetGpioDir();
        case DRV_BUS_UART:  return DrvFs_GetUartDir();
        case DRV_BUS_POWER: return DrvFs_GetPowerDir();
        case DRV_BUS_USB:   return DrvFs_GetUsbDir();
        case DRV_BUS_TIMER: return DrvFs_GetTimerDir();
        default:            return DrvFs_GetDriverDir();
    }
}

const char* DrvDevice_GetBusName(DrvBusType_t bus)
{
    if (bus < DRV_BUS_MAX) {
        return g_BusNames[bus];
    }
    return "unknown";
}

void DrvDevice_List(DrvDeviceListCallback_t callback, void *userData)
{
    uint8_t i;
    
    if (!callback) return;
    
    for (i = 0; i < g_DeviceCount; i++) {
        if (g_Devices[i]) {
            callback(g_Devices[i], userData);
        }
    }
}

int DrvDevice_GetCount(void)
{
    return g_DeviceCount;
}

DrvDevice_t** DrvDevice_GetList(int *count)
{
    if (count) {
        *count = g_DeviceCount;
    }
    return g_Devices;
}
