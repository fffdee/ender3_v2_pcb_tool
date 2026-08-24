/**
 *****************************************************************************
 * @file     drv_usb_cdc.c
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    USB CDC驱动框架适配层实现
 *****************************************************************************
 */

#include "drv_usb_cdc.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "otg_device_cdc.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/**
 * @brief USB CDC驱动私有数据
 */
typedef struct {
    bool initialized;
    char name[32];
} UsbCdcPrivData_t;

// 全局私有数据
static UsbCdcPrivData_t g_usb_cdc_priv = {
    .initialized = false,
    .name = "USB_CDC"
};

// 外部USB CDC设备实例
extern UsbCDC_t UsbCDC;

/*****************************************************************************
 * 参数读取回调函数
 *****************************************************************************/

/**
 * @brief 获取设备名称
 */
static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    UsbCdcPrivData_t *priv = (UsbCdcPrivData_t *)userData;
    snprintf(buf, maxLen, "%s", priv->name);
    return strlen(buf);
}

/**
 * @brief 获取连接状态
 */
static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    const char *status = UsbCDC.IsConnected ? "connected" : "disconnected";
    snprintf(buf, maxLen, "%s", status);
    return strlen(buf);
}

/**
 * @brief 获取波特率
 */
static int param_get_baudrate(char *buf, uint16_t maxLen, void *userData)
{
    snprintf(buf, maxLen, "%lu", (unsigned long)UsbCDC.LineCoding.dwDTERate);
    return strlen(buf);
}

/**
 * @brief 设置波特率
 */
static int param_set_baudrate(const char *value, void *userData)
{
    uint32_t baudrate;
    
    baudrate = (uint32_t)atoi(value);
    if (baudrate >= 300 && baudrate <= 115200) {
        UsbCDC.LineCoding.dwDTERate = baudrate;
        return 0;
    }
    return -1;
}

/**
 * @brief 获取数据位
 */
static int param_get_databits(char *buf, uint16_t maxLen, void *userData)
{
    snprintf(buf, maxLen, "%u", UsbCDC.LineCoding.bDataBits);
    return strlen(buf);
}

/**
 * @brief 设置数据位
 */
static int param_set_databits(const char *value, void *userData)
{
    uint8_t databits = (uint8_t)atoi(value);
    
    if (databits == 5 || databits == 6 || databits == 7 || databits == 8) {
        UsbCDC.LineCoding.bDataBits = databits;
        return 0;
    }
    return -1;
}

/**
 * @brief 获取停止位
 */
static int param_get_stopbits(char *buf, uint16_t maxLen, void *userData)
{
    const char *stopbits[] = {"1", "1.5", "2"};
    uint8_t idx = UsbCDC.LineCoding.bCharFormat;
    
    if (idx < 3) {
        snprintf(buf, maxLen, "%s", stopbits[idx]);
        return strlen(buf);
    }
    return -1;
}

/**
 * @brief 设置停止位
 */
static int param_set_stopbits(const char *value, void *userData)
{
    if (strcmp(value, "1") == 0) {
        UsbCDC.LineCoding.bCharFormat = 0;
        return 0;
    } else if (strcmp(value, "1.5") == 0) {
        UsbCDC.LineCoding.bCharFormat = 1;
        return 0;
    } else if (strcmp(value, "2") == 0) {
        UsbCDC.LineCoding.bCharFormat = 2;
        return 0;
    }
    return -1;
}

/**
 * @brief 获取校验位
 */
static int param_get_parity(char *buf, uint16_t maxLen, void *userData)
{
    const char *parity[] = {"none", "odd", "even", "mark", "space"};
    uint8_t idx = UsbCDC.LineCoding.bParityType;
    
    if (idx < 5) {
        snprintf(buf, maxLen, "%s", parity[idx]);
        return strlen(buf);
    }
    return -1;
}

/**
 * @brief 设置校验位
 */
static int param_set_parity(const char *value, void *userData)
{
    if (strcmp(value, "none") == 0) {
        UsbCDC.LineCoding.bParityType = 0;
        return 0;
    } else if (strcmp(value, "odd") == 0) {
        UsbCDC.LineCoding.bParityType = 1;
        return 0;
    } else if (strcmp(value, "even") == 0) {
        UsbCDC.LineCoding.bParityType = 2;
        return 0;
    } else if (strcmp(value, "mark") == 0) {
        UsbCDC.LineCoding.bParityType = 3;
        return 0;
    } else if (strcmp(value, "space") == 0) {
        UsbCDC.LineCoding.bParityType = 4;
        return 0;
    }
    return -1;
}

/**
 * @brief 获取接收缓冲区数据量
 */
static int param_get_rx_count(char *buf, uint16_t maxLen, void *userData)
{
    snprintf(buf, maxLen, "%u", UsbCDC.RxCount);
    return strlen(buf);
}

/**
 * @brief 获取发送缓冲区数据量
 */
static int param_get_tx_count(char *buf, uint16_t maxLen, void *userData)
{
    snprintf(buf, maxLen, "%u", UsbCDC.TxCount);
    return strlen(buf);
}

/**
 * @brief 清空缓冲区命令
 */
static int param_cmd_flush(const char *value, void *userData)
{
    if (strcmp(value, "rx") == 0) {
        OTG_DeviceCDC_FlushRxBuffer();
        return 0;
    } else if (strcmp(value, "tx") == 0) {
        OTG_DeviceCDC_FlushTxBuffer();
        return 0;
    } else if (strcmp(value, "all") == 0) {
        OTG_DeviceCDC_FlushRxBuffer();
        OTG_DeviceCDC_FlushTxBuffer();
        return 0;
    }
    return -1;
}

/*****************************************************************************
 * 参数定义表
 *****************************************************************************/
static const FsParamDef_t usb_cdc_params[] = {
    {
        .name = "name",
        .desc = "设备名称",
        .get = param_get_name,
        .set = NULL,
    },
    {
        .name = "status",
        .desc = "连接状态(connected/disconnected)",
        .get = param_get_status,
        .set = NULL,
    },
    {
        .name = "baudrate",
        .desc = "波特率(300-115200)",
        .get = param_get_baudrate,
        .set = param_set_baudrate,
    },
    {
        .name = "databits",
        .desc = "数据位(5/6/7/8)",
        .get = param_get_databits,
        .set = param_set_databits,
    },
    {
        .name = "stopbits",
        .desc = "停止位(1/1.5/2)",
        .get = param_get_stopbits,
        .set = param_set_stopbits,
    },
    {
        .name = "parity",
        .desc = "校验位(none/odd/even/mark/space)",
        .get = param_get_parity,
        .set = param_set_parity,
    },
    {
        .name = "rx_count",
        .desc = "接收缓冲区数据量",
        .get = param_get_rx_count,
        .set = NULL,
    },
    {
        .name = "tx_count",
        .desc = "发送缓冲区数据量",
        .get = param_get_tx_count,
        .set = NULL,
    },
    {
        .name = "flush",
        .desc = "清空缓冲区(rx/tx/all)",
        .get = NULL,
        .set = param_cmd_flush,
    },
    FS_PARAM_END
};

/*****************************************************************************
 * 驱动操作函数
 *****************************************************************************/

/**
 * @brief USB CDC驱动初始化
 */
static int usb_cdc_drv_init(void *priv)
{
    UsbCdcPrivData_t *usb_priv = (UsbCdcPrivData_t *)priv;
    
    if (usb_priv->initialized) {
        return 0;  // 已初始化
    }
    
    // 调用底层USB CDC初始化
    if (!OTG_DeviceCDC_Init()) {
        return -1;
    }
    
    usb_priv->initialized = true;
    return 0;
}

/**
 * @brief USB CDC驱动反初始化
 */
static int usb_cdc_drv_deinit(void *priv)
{
    UsbCdcPrivData_t *usb_priv = (UsbCdcPrivData_t *)priv;
    
    OTG_DeviceCDC_DeInit();
    usb_priv->initialized = false;
    return 0;
}

/**
 * @brief 打开USB CDC设备
 */
static int usb_cdc_drv_open(void *priv)
{
    // USB CDC设备无需特殊打开操作
    return 0;
}

/**
 * @brief 关闭USB CDC设备
 */
static int usb_cdc_drv_close(void *priv)
{
    // USB CDC设备无需特殊关闭操作
    return 0;
}

/**
 * @brief 从USB CDC读取数据
 * @param priv 私有数据
 * @param buf 读取缓冲区
 * @param len 读取长度
 * @return 实际读取的字节数
 */
static int usb_cdc_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint32_t count;
    
    if (!UsbCDC.IsConnected) {
        return 0;  // 未连接，返回0
    }
    
    count = (len < UsbCDC.RxCount) ? len : UsbCDC.RxCount;
    
    for (i = 0; i < count; i++) {
        buf[i] = OTG_DeviceCDC_GetChar();
    }
    
    return (int)count;
}

/**
 * @brief 向USB CDC写入数据
 * @param priv 私有数据
 * @param buf 写入缓冲区
 * @param len 写入长度
 * @return 实际写入的字节数
 */
static int usb_cdc_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    uint32_t i;
    
    if (!UsbCDC.IsConnected) {
        return 0;  // 未连接，返回0
    }
    
    for (i = 0; i < len; i++) {
        OTG_DeviceCDC_SendChar(buf[i]);
    }
    
    return (int)len;
}

/**
 * @brief IOCTL控制命令
 * @param priv 私有数据
 * @param cmd 命令码
 * @param arg 参数
 * @return 0成功, 负值失败
 * 
 * 支持的命令:
 *   0x01 - 检查连接状态 (arg=NULL, 返回值: 0=未连接, 1=已连接)
 *   0x02 - 刷新接收缓冲区
 *   0x03 - 刷新发送缓冲区
 *   0x04 - 获取可用数据量 (arg=uint32_t*, 返回接收缓冲区数据量)
 */
static int usb_cdc_drv_ioctl(void *priv, uint32_t cmd, void *arg)
{
    switch (cmd) {
        case 0x01:  // 检查连接状态
            return UsbCDC.IsConnected ? 1 : 0;
            
        case 0x02:  // 刷新接收缓冲区
            OTG_DeviceCDC_FlushRxBuffer();
            return 0;
            
        case 0x03:  // 刷新发送缓冲区
            OTG_DeviceCDC_FlushTxBuffer();
            return 0;
            
        case 0x04:  // 获取可用数据量
            if (arg != NULL) {
                *(uint32_t *)arg = UsbCDC.RxCount;
                return 0;
            }
            return -1;
            
        default:
            return -1;
    }
}

/*****************************************************************************
 * 驱动设备结构定义
 *****************************************************************************/
/* 注意：不能用const，因为需要在运行时修改isRegistered/fsNode等字段 */
static DrvDevice_t usb_cdc_driver = {
    .name = "cdc",
    .bus = DRV_BUS_USB,  // USB总线
    .init = usb_cdc_drv_init,
    .deinit = usb_cdc_drv_deinit,
    .open = usb_cdc_drv_open,
    .close = usb_cdc_drv_close,
    .read = usb_cdc_drv_read,
    .write = usb_cdc_drv_write,
    .ioctl = usb_cdc_drv_ioctl,
    .params = usb_cdc_params,
    .privData = &g_usb_cdc_priv,
};

/*****************************************************************************
 * 对外注册接口
 *****************************************************************************/

/**
 * @brief 注册USB CDC驱动到驱动框架
 * @return 0成功, 负值失败
 * 
 * 注册后创建以下文件系统节点:
 *   /driver/usb/cdc/
 *   ├── name
 *   ├── status
 *   ├── baudrate
 *   ├── databits
 *   ├── stopbits
 *   ├── parity
 *   ├── rx_count
 *   ├── tx_count
 *   └── flush
 */
int UsbCdc_DrvRegister(void)
{
    return DrvDevice_Register(&usb_cdc_driver);
}
