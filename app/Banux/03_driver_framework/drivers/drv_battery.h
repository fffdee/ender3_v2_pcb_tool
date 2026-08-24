#ifndef DRV_BATTERY_H
#define DRV_BATTERY_H

#include <stdint.h>

/**
 * @file drv_battery.h
 * @brief 电池管理驱动框架适配层
 * 
 * 将底层电池驱动(battery_drv.c)适配到驱动框架，提供统一的访问接口
 * 
 * 注册后的文件系统路径:
 *   /driver/power/battery/
 *   ├── name           (只读) - 设备名称 "Battery_Manager"
 *   ├── soc            (只读) - 电量百分比 0~100
 *   ├── voltage        (只读) - 实时电压(V)
 *   ├── status         (只读) - 工作状态 "normal"/"low"/"critical"
 *   ├── full_volt      (只读) - 满电电压 4.2V
 *   ├── empty_volt     (只读) - 空电电压 3.0V
 *   └── refresh        (只写) - 强制刷新电量(写入任意值触发)
 * 
 * Shell命令示例:
 *   cat /driver/power/battery/soc        # 查看电量百分比
 *   cat /driver/power/battery/voltage    # 查看实时电压
 *   cat /driver/power/battery/status     # 查看电池状态
 *   echo 1 > /driver/power/battery/refresh  # 刷新电量数据
 */

/**
 * @brief 注册电池管理驱动到驱动框架
 * @return 0成功, 负值失败
 */
int Battery_DrvRegister(void);

#endif /* DRV_BATTERY_H */
