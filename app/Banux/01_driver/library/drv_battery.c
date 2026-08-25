#include "drv_battery.h"
#include "drv_device.h"
#include "drv_fs.h"
#include "battery_drv.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

/**
 * @brief 电池管理驱动私有数据
 */
typedef struct {
    uint8_t  soc;           // 电量百分比 State of Charge (0~100)
    float    voltage;       // 实时电压(V)
    bool     initialized;   // 初始化标志
    uint32_t last_update;   // 上次更新时间戳(用于缓存优化)
} BatteryPrivData_t;

// 全局私有数据
static BatteryPrivData_t g_battery_priv = {
    .soc = 0,
    .voltage = 0.0f,
    .initialized = false,
    .last_update = 0,
};

/*****************************************************************************
 * 参数读取回调函数
 *****************************************************************************/

/**
 * @brief 获取设备名称
 */
static int param_get_name(char *buf, uint16_t maxLen, void *userData)
{
    snprintf(buf, maxLen, "Battery_Manager");
    return strlen(buf);
}

/**
 * @brief 获取电量百分比
 */
static int param_get_soc(char *buf, uint16_t maxLen, void *userData)
{
    BatteryPrivData_t *priv = (BatteryPrivData_t *)userData;
    
    // 读取最新电量
    priv->soc = battery_get_soc();
    
    snprintf(buf, maxLen, "%u", priv->soc);
    return strlen(buf);
}

/**
 * @brief 获取实时电压
 */
static int param_get_voltage(char *buf, uint16_t maxLen, void *userData)
{
    BatteryPrivData_t *priv = (BatteryPrivData_t *)userData;
    
    // 读取最新电压
    priv->voltage = battery_get_volt();
    
    // 将浮点数转换为整数表示 (millivolts)
    int millivolts = (int)(priv->voltage * 1000);
    int volts = millivolts / 1000;
    int decimal = (millivolts % 1000) / 10;  // 保留两位小数
    
    snprintf(buf, maxLen, "%d.%02d", volts, decimal);
    return strlen(buf);
}

/**
 * @brief 获取电池状态
 * @note 根据SOC返回状态: normal(>20%), low(10~20%), critical(<10%)
 */
static int param_get_status(char *buf, uint16_t maxLen, void *userData)
{
    BatteryPrivData_t *priv = (BatteryPrivData_t *)userData;
    
    // 读取最新电量
    priv->soc = battery_get_soc();
    
    const char *status;
    if (priv->soc > 20) {
        status = "normal";
    } else if (priv->soc >= 10) {
        status = "low";
    } else {
        status = "critical";
    }
    
    snprintf(buf, maxLen, "%s", status);
    return strlen(buf);
}

/**
 * @brief 获取满电电压
 */
static int param_get_full_volt(char *buf, uint16_t maxLen, void *userData)
{
    // 将浮点数转换为整数表示
    int millivolts = (int)(FULL_VOLT * 1000);
    int volts = millivolts / 1000;
    int decimal = (millivolts % 1000) / 100;  // 保留一位小数
    
    snprintf(buf, maxLen, "%d.%01d", volts, decimal);
    return strlen(buf);
}

/**
 * @brief 获取空电电压
 */
static int param_get_empty_volt(char *buf, uint16_t maxLen, void *userData)
{
    // 将浮点数转换为整数表示
    int millivolts = (int)(EMPTY_VOLT * 1000);
    int volts = millivolts / 1000;
    int decimal = (millivolts % 1000) / 100;  // 保留一位小数
    
    snprintf(buf, maxLen, "%d.%01d", volts, decimal);
    return strlen(buf);
}

/*****************************************************************************
 * 参数写入回调函数
 *****************************************************************************/

/**
 * @brief 刷新电量数据(写入任意值触发)
 */
static int param_cmd_refresh(const char *value, void *userData)
{
    BatteryPrivData_t *priv = (BatteryPrivData_t *)userData;
    
    // 强制刷新电量和电压
    priv->soc = battery_get_soc();
    priv->voltage = battery_get_volt();
    
    return 0;  // 成功
}

/*****************************************************************************
 * 参数定义表
 *****************************************************************************/
static const FsParamDef_t battery_params[] = {
    {
        .name = "name",
        .desc = "设备名称",
        .get = param_get_name,
        .set = NULL,  // 只读
    },
    {
        .name = "soc",
        .desc = "电量百分比(0~100)",
        .get = param_get_soc,
        .set = NULL,  // 只读
    },
    {
        .name = "voltage",
        .desc = "实时电压(V)",
        .get = param_get_voltage,
        .set = NULL,  // 只读
    },
    {
        .name = "status",
        .desc = "电池状态(normal/low/critical)",
        .get = param_get_status,
        .set = NULL,  // 只读
    },
    {
        .name = "full_volt",
        .desc = "满电电压(V)",
        .get = param_get_full_volt,
        .set = NULL,  // 只读
    },
    {
        .name = "empty_volt",
        .desc = "空电电压(V)",
        .get = param_get_empty_volt,
        .set = NULL,  // 只读
    },
    {
        .name = "refresh",
        .desc = "刷新电量数据(写入任意值)",
        .get = NULL,  // 只写
        .set = param_cmd_refresh,
    },
    FS_PARAM_END // 结束标记
};

/*****************************************************************************
 * 驱动操作函数
 *****************************************************************************/

/**
 * @brief 电池管理驱动初始化
 */
static int battery_drv_init(void *priv)
{
    BatteryPrivData_t *battery_priv = (BatteryPrivData_t *)priv;
    
    if (battery_priv->initialized) {
        return 0;  // 已初始化
    }
    
    // 延迟读取电量，避免在main()中调用Shell_Printf导致问题
    // 首次读取将在第一次参数访问时进行
    battery_priv->soc = 0;
    battery_priv->voltage = 0.0f;
    battery_priv->initialized = true;
    
    return 0;
}

/**
 * @brief 电池管理驱动反初始化
 */
static int battery_drv_deinit(void *priv)
{
    BatteryPrivData_t *battery_priv = (BatteryPrivData_t *)priv;
    battery_priv->initialized = false;
    return 0;
}

/**
 * @brief 打开电池设备(可选实现)
 */
static int battery_drv_open(void *priv)
{
    // 电池管理无需特殊打开操作
    return 0;
}

/**
 * @brief 关闭电池设备(可选实现)
 */
static int battery_drv_close(void *priv)
{
    // 电池管理无需特殊关闭操作
    return 0;
}

/**
 * @brief 读取电池数据
 * @param priv 私有数据
 * @param buf 读取缓冲区
 * @param len 读取长度
 * @return 实际读取的字节数
 * @note 返回格式化的电池信息字符串
 */
static int battery_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    BatteryPrivData_t *battery_priv = (BatteryPrivData_t *)priv;
    uint16_t mv;
    int written;
    
    // 刷新数据
    battery_priv->soc = battery_get_soc();
    mv = battery_get_volt_mv();
    
    // 格式化输出 (avoid %f which triggers _printf_float -> libm FPU crash)
    written = snprintf((char *)buf, len,
        "Battery Info:\n"
        "  SOC: %u%%\n"
        "  Voltage: %u.%02uV\n"
        "  Status: %s\n",
        battery_priv->soc,
        (unsigned)(mv / 1000u),
        (unsigned)((mv % 1000u) / 10u),
        (battery_priv->soc > 20) ? "Normal" :
        (battery_priv->soc >= 10) ? "Low" : "Critical"
    );
    
    return (written > 0) ? written : 0;
}

/**
 * @brief IOCTL控制命令
 * @param priv 私有数据
 * @param cmd 命令码
 * @param arg 参数
 * @return 0成功, 负值失败
 * 
 * 支持的命令:
 *   0x01 - 刷新电量数据
 *   0x02 - 获取SOC到arg指向的uint8_t*
 *   0x03 - 获取电压到arg指向的float*
 */
static int battery_drv_ioctl(void *priv, uint32_t cmd, void *arg)
{
    BatteryPrivData_t *battery_priv = (BatteryPrivData_t *)priv;
    
    switch (cmd) {
        case 0x01:  // 刷新电量数据
            battery_priv->soc = battery_get_soc();
            battery_priv->voltage = battery_get_volt();
            return 0;
            
        case 0x02:  // 获取SOC
            if (arg != NULL) {
                *(uint8_t *)arg = battery_get_soc();
                return 0;
            }
            return -1;
            
        case 0x03:  // 获取电压
            if (arg != NULL) {
                *(float *)arg = battery_get_volt();
                return 0;
            }
            return -1;
            
        default:
            return -1;  // 不支持的命令
    }
}

/*****************************************************************************
 * 驱动设备结构定义
 *****************************************************************************/
/* 注意：不能用const，因为需要在运行时修改isRegistered/fsNode等字段 */
static DrvDevice_t battery_driver = {
    .name = "battery",
    .bus = DRV_BUS_POWER,  // 电源总线
    .init = battery_drv_init,
    .deinit = battery_drv_deinit,
    .open = battery_drv_open,
    .close = battery_drv_close,
    .read = battery_drv_read,
    .write = NULL,  // 电池不支持写入
    .ioctl = battery_drv_ioctl,
    .params = battery_params,
    .privData = &g_battery_priv,
};

/*****************************************************************************
 * 对外注册接口
 *****************************************************************************/

/**
 * @brief 注册电池管理驱动到驱动框架
 * @return 0成功, 负值失败
 * 
 * 注册后创建以下文件系统节点:
 *   /driver/power/battery/
 *   ├── name
 *   ├── soc
 *   ├── voltage
 *   ├── status
 *   ├── full_volt
 *   ├── empty_volt
 *   └── refresh
 */
int Battery_DrvRegister(void)
{
    return DrvDevice_Register(&battery_driver);
}
