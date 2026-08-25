/**
 *****************************************************************************
 * @file     drv_usb_cdc.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    USB CDC驱动框架适配层
 *****************************************************************************
 * @attention
 *
 * 将USB CDC设备驱动适配到驱动框架，提供统一的访问接口
 * 
 * 注册后的文件系统路径:
 *   /driver/usb/cdc/
 *   ├── name           (只读) - 设备名称 "USB_CDC"
 *   ├── status         (只读) - 连接状态 "connected"/"disconnected"
 *   ├── baudrate       (读写) - 波特率
 *   ├── databits       (读写) - 数据位 (5/6/7/8)
 *   ├── stopbits       (读写) - 停止位 (0/1/2)
 *   ├── parity         (读写) - 校验位 (none/odd/even/mark/space)
 *   ├── rx_count       (只读) - 接收缓冲区数据量
 *   ├── tx_count       (只读) - 发送缓冲区数据量
 *   └── flush          (只写) - 清空缓冲区 (写入"rx"/"tx"/"all")
 * 
 * Shell命令示例:
 *   cat /driver/usb/cdc/status        # 查看连接状态
 *   cat /driver/usb/cdc/baudrate      # 查看波特率
 *   echo 9600 > /driver/usb/cdc/baudrate  # 设置波特率
 *   cat /driver/usb/cdc/rx_count      # 查看接收数据量
 *   echo all > /driver/usb/cdc/flush  # 清空所有缓冲区
 * 
 * 设备操作:
 *   read()  - 从USB CDC读取数据
 *   write() - 向USB CDC写入数据
 *   ioctl() - 控制命令:
 *             0x01: 检查连接状态
 *             0x02: 刷新接收缓冲区
 *             0x03: 刷新发送缓冲区
 *             0x04: 获取可用数据量
 *****************************************************************************
 */

#ifndef DRV_USB_CDC_H
#define DRV_USB_CDC_H

#include <stdint.h>

/**
 * @brief 注册USB CDC驱动到驱动框架
 * @return 0成功, 负值失败
 */
int UsbCdc_DrvRegister(void);

#endif /* DRV_USB_CDC_H */
