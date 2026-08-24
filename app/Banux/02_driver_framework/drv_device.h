/**
 ******************************************************************************
 * @file    drv_device.h
 * @brief   驱动设备注册框架 - 类Linux驱动模型
 *
 * 实现:
 *   1. 驱动抽象层: 标准驱动接口 (init/open/close/read/write/ioctl)
 *   2. 设备注册: 将驱动注册到文件系统 /driver/<bus>/<name>
 *   3. 参数自动注册: 根据参数定义自动创建参数节点
 *   4. 总线类型分类: SPI/I2C/I2S/SDIO/GPIO/UART/POWER/USB
 *
 * 使用示例:
 *   static const FsParamDef_t st7735_params[] = {
 *       FS_PARAM_DEF("name",   "驱动名称", get_name, NULL),
 *       FS_PARAM_DEF("width",  "LCD宽度",  get_width, set_width),
 *       FS_PARAM_DEF("height", "LCD高度",  get_height, set_height),
 *       FS_PARAM_END
 *   };
 *
 *   static const DrvDevice_t st7735_drv = {
 *       .name = "st7735",
 *       .bus = DRV_BUS_SPI,
 *       .init = st7735_drv_init,
 *       .params = st7735_params,
 *   };
 *
 *   DrvDevice_Register(&st7735_drv);
 ******************************************************************************
 */
#ifndef __DRV_DEVICE_H__
#define __DRV_DEVICE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "banux_config.h"
#include "drv_fs.h"

/*===========================================================================
 * 总线类型
 *===========================================================================*/
typedef enum {
    DRV_BUS_SPI = 0,
    DRV_BUS_I2C,
    DRV_BUS_I2S,
    DRV_BUS_SDIO,
    DRV_BUS_GPIO,
    DRV_BUS_UART,
    DRV_BUS_POWER,
    DRV_BUS_USB,
    DRV_BUS_MAX
} DrvBusType_t;

/*===========================================================================
 * 驱动操作接口类型
 *===========================================================================*/
typedef int (*DrvInit_t)(void *priv);
typedef int (*DrvDeinit_t)(void *priv);
typedef int (*DrvOpen_t)(void *priv);
typedef int (*DrvClose_t)(void *priv);
typedef int (*DrvRead_t)(void *priv, uint8_t *buf, uint32_t len);
typedef int (*DrvWrite_t)(void *priv, const uint8_t *buf, uint32_t len);
typedef int (*DrvIoctl_t)(void *priv, uint32_t cmd, void *arg);

/*===========================================================================
 * 驱动设备结构
 *===========================================================================*/
typedef struct DrvDevice {
    /* 基本信息 */
    const char         *name;
    const char         *desc;
    DrvBusType_t        bus;

    /* 驱动操作接口 */
    DrvInit_t           init;
    DrvDeinit_t         deinit;
    DrvOpen_t           open;
    DrvClose_t          close;
    DrvRead_t           read;
    DrvWrite_t          write;
    DrvIoctl_t          ioctl;

    /* 参数定义列表 */
    const FsParamDef_t *params;

    /* 私有数据 */
    void               *privData;

    /* 运行时状态 (由系统管理) */
    FsNode_t           *fsNode;
    bool                isRegistered;
    bool                isOpened;
} DrvDevice_t;

/*===========================================================================
 * 公共API
 *===========================================================================*/

/**
 * @brief  初始化驱动管理系统
 * @note   会自动调用 DrvFs_Init()
 */
int DrvDevice_Init(void);

/**
 * @brief  注册驱动设备
 * @note   自动在对应总线目录创建设备节点和参数节点, 并调用init
 */
int DrvDevice_Register(DrvDevice_t *dev);

/**
 * @brief  注销驱动设备
 */
int DrvDevice_Unregister(DrvDevice_t *dev);

/**
 * @brief  根据名称查找设备
 */
DrvDevice_t* DrvDevice_Find(const char *name);

/**
 * @brief  根据路径查找设备 (如 "/driver/spi/st7735")
 */
DrvDevice_t* DrvDevice_FindByPath(const char *path);

/**
 * @brief  获取总线类型对应目录节点
 */
FsNode_t* DrvDevice_GetBusDir(DrvBusType_t bus);

/**
 * @brief  获取总线类型名称
 */
const char* DrvDevice_GetBusName(DrvBusType_t bus);

/**
 * @brief  列出所有已注册设备
 */
typedef void (*DrvDeviceListCallback_t)(DrvDevice_t *dev, void *userData);
void DrvDevice_List(DrvDeviceListCallback_t callback, void *userData);

int DrvDevice_GetCount(void);
DrvDevice_t** DrvDevice_GetList(int *count);

/*===========================================================================
 * 便捷宏定义
 *===========================================================================*/
#define DRV_DEVICE_DEF(n, d, b, i) \
    { .name = n, .desc = d, .bus = b, .init = i, \
      .deinit = NULL, .open = NULL, .close = NULL, \
      .read = NULL, .write = NULL, .ioctl = NULL, \
      .params = NULL, .privData = NULL, \
      .fsNode = NULL, .isRegistered = FALSE, .isOpened = FALSE }

#define DRV_PARAM_RO(n, d, g)       FS_PARAM_DEF(n, d, g, NULL)
#define DRV_PARAM_RW(n, d, g, s)    FS_PARAM_DEF(n, d, g, s)

#if !DRV_DEVICE_EN
#define DrvDevice_Init()                    (0)
#define DrvDevice_Register(dev)             (0)
#define DrvDevice_Unregister(dev)           (0)
#define DrvDevice_Find(name)                ((DrvDevice_t*)NULL)
#define DrvDevice_FindByPath(path)          ((DrvDevice_t*)NULL)
#define DrvDevice_GetBusDir(bus)            ((FsNode_t*)NULL)
#define DrvDevice_GetBusName(bus)           ("")
#define DrvDevice_List(cb, ud)              ((void)0)
#define DrvDevice_GetCount()                (0)
#define DrvDevice_GetList(count)            ((DrvDevice_t**)NULL)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __DRV_DEVICE_H__ */
