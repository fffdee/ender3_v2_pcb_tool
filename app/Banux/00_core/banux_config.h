/**
 ******************************************************************************
 * @file    banux_config.h
 * @brief   BanUX 通用框架配置
 *
 * 所有功能开关集中管理，板级 product_def.h 可覆盖默认值。
 * 从 BanBox banux_config.h 合并硬件开关，保持 #ifndef 防护。
 ******************************************************************************
 */
#ifndef __BANUX_CONFIG_H__
#define __BANUX_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 核心框架开关
 *===========================================================================*/
#ifndef VFS_EN
#define VFS_EN                      1   /* 虚拟文件系统 */
#endif

#ifndef BG_EVENT_EN
#define BG_EVENT_EN                 1   /* 事件发布/订阅 */
#endif

#ifndef DRV_DEVICE_EN
#define DRV_DEVICE_EN               1   /* 驱动设备注册框架 */
#endif

#ifndef BANUX_IO_EN
#define BANUX_IO_EN                 1   /* 路径式驱动 read/write API */
#endif

#ifndef COMMAND_PARSER_EN
#define COMMAND_PARSER_EN           1   /* UTF-8 脚本及非阻塞定时执行 */
#endif

#ifndef TIMER1MS_EN
#define TIMER1MS_EN                 COMMAND_PARSER_EN /* SysTick 1 ms 驱动 */
#endif

#ifndef BANUX_GCODE_EN
#define BANUX_GCODE_EN              1
#endif

#ifndef BANUX_WIRELESS_CONTROL_EN
#define BANUX_WIRELESS_CONTROL_EN   1   /* ESP8266 WiFi bridge control */
#endif

#ifndef GCODE_X_STEPS_PER_MM
#define GCODE_X_STEPS_PER_MM        80
#endif
#ifndef GCODE_Y_STEPS_PER_MM
#define GCODE_Y_STEPS_PER_MM        80
#endif
#ifndef GCODE_Z_STEPS_PER_MM
#define GCODE_Z_STEPS_PER_MM        400
#endif
#ifndef GCODE_E_STEPS_PER_MM
#define GCODE_E_STEPS_PER_MM        95
#endif

#ifndef BANUX_INTERNAL_FLASH_FS_EN
#define BANUX_INTERNAL_FLASH_FS_EN  BANUX_FATFS_EN /* 独立挂载 20 KB Flash FAT12 */
#endif

#ifndef STEPPER_LIMIT_ACTIVE_HIGH
#define STEPPER_LIMIT_ACTIVE_HIGH   1   /* Creality NC endstop: open/high means hit */
#endif

#ifndef SYS_LED_EN
#define SYS_LED_EN                  1   /* 系统状态 LED */
#endif

#ifndef EFFECT_GRAPHICS_EN
#define EFFECT_GRAPHICS_EN          1   /* 效果图组件 */
#endif

/*===========================================================================
 * VFS 配置
 *===========================================================================*/
#ifndef VFS_MAX_PATH_LEN
#define VFS_MAX_PATH_LEN            64
#endif

#ifndef VFS_MAX_NAME_LEN
#define VFS_MAX_NAME_LEN            16
#endif

#ifndef VFS_MAX_CHILDREN
#define VFS_MAX_CHILDREN            24
#endif

#ifndef VFS_MAX_PARAM_LEN
#define VFS_MAX_PARAM_LEN           32
#endif

#ifndef VFS_MAX_NODES
#define VFS_MAX_NODES               128
#endif

/*===========================================================================
 * 驱动框架配置
 *===========================================================================*/
#ifndef DRV_DEVICE_MAX
#define DRV_DEVICE_MAX              16
#endif

/*===========================================================================
 * 事件系统配置
 *===========================================================================*/
#ifndef BG_EVENT_MAX_SUBSCRIBERS
#define BG_EVENT_MAX_SUBSCRIBERS    32
#endif

#ifndef BG_EVENT_MAX_REENTRY
#define BG_EVENT_MAX_REENTRY        4
#endif

/*===========================================================================
 * 硬件驱动框架开关 (板级 product_def.h 可覆盖)
 *===========================================================================*/
#ifndef HW_DRV_LCD_EN
#define HW_DRV_LCD_EN              0   /* ST7735 LCD 驱动 */
#endif

#ifndef HW_DRV_FLASH_NOR_EN
#define HW_DRV_FLASH_NOR_EN        0   /* W25Qxx NOR Flash VFS 驱动 */
#endif

#ifndef HW_DRV_FLASH_NAND_EN
#define HW_DRV_FLASH_NAND_EN       0   /* W25N02 NAND Flash VFS 驱动 */
#endif

#ifndef HW_DRV_PSRAM_EN
#define HW_DRV_PSRAM_EN            0   /* ESP-PSRAM64H PSRAM VFS 驱动 */
#endif

#ifndef HW_DRV_SDCARD_EN
#define HW_DRV_SDCARD_EN           0   /* SD Card VFS 驱动 */
#endif

#ifndef HW_DRV_BATTERY_EN
#define HW_DRV_BATTERY_EN          1   /* 电池管理 VFS 驱动 */
#endif

#ifndef HW_DRV_USB_CDC_EN
#define HW_DRV_USB_CDC_EN          1   /* USB CDC VFS 驱动 */
#endif

#ifndef HW_DRV_BT_EN
#define HW_DRV_BT_EN               1   /* 蓝牙/BLE VFS 驱动 */
#endif

/*===========================================================================
 * 板级设备实例开关
 *===========================================================================*/
#ifndef HW_FLASH0_EN
#define HW_FLASH0_EN               0   /* NOR Flash #0 */
#endif

#ifndef HW_FLASH1_EN
#define HW_FLASH1_EN               0   /* NOR Flash #1 */
#endif

#ifndef HW_NAND0_EN
#define HW_NAND0_EN                0   /* W25N02 NAND Flash */
#endif

#ifndef HW_PSRAM0_EN
#define HW_PSRAM0_EN               0   /* ESP-PSRAM64H PSRAM */
#endif

#ifndef HW_SDCARD0_EN
#define HW_SDCARD0_EN              0   /* SD Card over SDIO */
#endif

#ifndef HW_VOLUME_ADC_EN
#define HW_VOLUME_ADC_EN           0   /* 音量旋钮 ADC */
#endif

/*===========================================================================
 * 功能模块开关
 *===========================================================================*/
#ifndef LINEIN_EN
#define LINEIN_EN                  0   /* 双 Line-In 输入 */
#endif

#ifndef LINE1_EN
#define LINE1_EN                   0   /* Line 输入 1 */
#endif

#ifndef LINE2_EN
#define LINE2_EN                   0   /* Line 输入 2 */
#endif

#ifndef MIC_EN
#define MIC_EN                     0   /* 麦克风输入 */
#endif

#ifndef LINE1_INPUT_DETECT_EN
#define LINE1_INPUT_DETECT_EN      0   /* Line1 插入检测 */
#endif

#ifndef LINE2_INPUT_DETECT_EN
#define LINE2_INPUT_DETECT_EN      0   /* Line2 插入检测 */
#endif

#ifndef MIC_INPUT_DETECT_EN
#define MIC_INPUT_DETECT_EN        0   /* 麦克风插入检测 */
#endif

#ifndef USB_EN
#define USB_EN                     HW_DRV_USB_CDC_EN
#endif

#ifndef BANUX_FATFS_EN
#define BANUX_FATFS_EN             1   /* Banux FatFs 系统组件 */
#endif

#ifndef FLASH_TEST_EN
#define FLASH_TEST_EN              0   /* Flash 测试模块 */
#endif

#ifndef BOOTLOADER_EN
#define BOOTLOADER_EN              0   /* Bootloader 支持 */
#endif

#ifndef POWER_ON_MUSIC_EN
#define POWER_ON_MUSIC_EN          0   /* 开机音乐 */
#endif

#ifndef BUTTON_POWER_ENABLE
#define BUTTON_POWER_ENABLE        0   /* 按钮开机控制 */
#endif

#ifndef REVERB_RAM_OPTIMIZE
#define REVERB_RAM_OPTIMIZE        0   /* Reverb RAM 优化 */
#endif

#ifndef LOOPER_STORAGE_TYPE
#define LOOPER_STORAGE_TYPE        0   /* 0=auto, 1=PSRAM, 2=NAND, 3=NOR */
#endif

/*===========================================================================
 * Shell 命令开关
 *===========================================================================*/
#ifndef HW_CMD_FLASH_EN
#define HW_CMD_FLASH_EN            (HW_DRV_FLASH_NOR_EN || HW_DRV_FLASH_NAND_EN)
#endif

#ifndef HW_CMD_PSRAM_EN
#define HW_CMD_PSRAM_EN            HW_DRV_PSRAM_EN
#endif

#ifndef HW_CMD_FAT_EN
#define HW_CMD_FAT_EN              BANUX_FATFS_EN
#endif

/*===========================================================================
 * 基础类型 (平台需提供 type.h 或自定义)
 *===========================================================================*/
#ifndef __BANUX_TYPES_DEFINED
#define __BANUX_TYPES_DEFINED
#include <stdint.h>
#include <stdbool.h>

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif
#endif /* __BANUX_TYPES_DEFINED */

/*===========================================================================
 * 调试输出 (平台需提供 DBG 宏或自定义)
 *===========================================================================*/
#ifndef DBG
#include <stdio.h>
#define DBG(fmt, ...)   printf(fmt, ##__VA_ARGS__)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BANUX_CONFIG_H__ */
