/**
 *****************************************************************************
 * @file     drv_framework.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    驱动框架统一头文件
 *****************************************************************************
 * @attention
 *
 * 包含此文件即可使用完整的驱动注册框架功能：
 * - 设备文件系统
 * - 驱动注册管理
 * - Shell文件系统命令
 *
 *****************************************************************************
 */

#ifndef __DRV_FRAMEWORK_H__
#define __DRV_FRAMEWORK_H__

#ifdef __cplusplus
extern "C" {
#endif

/* 核心模块 */
#include "drv_fs.h"
#include "drv_device.h"
#include "drv_init.h"

/* Shell命令集成 */
/* 注意: ShellFs_RegisterCommands() 需要在shell_fs_commands.c中实现 */

/*******************************************************************************
 * 框架初始化
 ******************************************************************************/

/**
 * @brief  初始化驱动框架
 * @note   应在Shell初始化后调用
 *
 * 使用方式 (纯Linux风格命令):
 *   pwd             打印当前路径
 *   cd <path>       切换目录
 *   cd ..           返回上级目录
 *   cd /            回到根目录
 *   ls              列出当前目录 (简洁)
 *   ls -l           详细列出目录
 *   tree            显示目录树
 *   cat <param>     读取参数
 *   echo <p> <v>    写入参数
 *   drivers         列出所有已注册驱动
 *
 * 示例:
 *   cd driver/spi/st7735    -> 切换到st7735设备目录
 *   ls -l                   -> 列出当前目录所有参数
 *   cat width               -> 读取width参数值
 *   echo width 128          -> 设置width为128
 */
static inline int DrvFramework_Init_Old(void)
{
    /* 使用新的初始化方式 */
    return DrvFramework_FullInit();
}

#ifdef __cplusplus
}
#endif

#endif /* __DRV_FRAMEWORK_H__ */
