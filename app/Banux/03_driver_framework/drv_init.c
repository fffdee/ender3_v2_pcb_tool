/**
 *****************************************************************************
 * @file     drv_init.c
 * @author   Ender-3 V2 Porting
 * @version  V1.0.0
 * @brief    驱动框架初始化（裁剪版：仅 VFS + Shell 命令模块）
 *
 * 由 BG Card 原版裁剪移植到 Ender-3 V2 APP 工程：
 *   1. 删除 W25QXX/W25N02/PSRAM/SDCARD/Battery/USB_CDC/BT/BLE 等
 *      与 Ender-3 V2 平台无关的硬件驱动注册表
 *   2. DrvFramework_Init     ：仅 Vfs_Init -> DrvFs_Init -> DrvDevice_Init
 *   3. DrvFramework_RegisterAll：仅注册 Shell 命令模块（sys/ls/pwd/cd/cat/echo/tree/drivers/boot）
 *   4. 删除 ShellFs_Init / Flash 管理器等不存在于本工程的模块依赖
 *****************************************************************************
 */

#include "drv_init.h"
#include "vfs.h"
#include "drv_fs.h"
#include "drv_device.h"
#include "drv_sdio.h"
#include "drv_uart.h"
#include "bg_shell.h"
#include "debug.h"

/*******************************************************************************
 * 驱动框架初始化函数
 ******************************************************************************/

/**
 * @brief  初始化驱动文件系统
 * @retval 0-成功, <0-失败
 */
int DrvFramework_Init(void)
{
    int ret;

    /* 1. 初始化VFS核心 */
    ret = Vfs_Init();
    if (ret != VFS_OK) {
        DBG("[DrvInit] VFS init failed!\n");
        return -1;
    }

    /* 2. 初始化驱动文件系统（创建/driver目录） */
    ret = DrvFs_Init();
    if (ret != FS_OK) {
        DBG("[DrvInit] DrvFs init failed!\n");
        return -2;
    }

    /* 3. 初始化设备管理系统 */
    ret = DrvDevice_Init();
    if (ret != 0) {
        DBG("[DrvInit] DrvDevice init failed!\n");
        return -3;
    }

    return 0;
}

/**
 * @brief  注册所有命令模块到 Shell 框架
 * @retval 0-成功, <0-失败
 *
 * @note   调用顺序:
 *         1. DrvFramework_Init() - 初始化 VFS 框架
 *         2. DrvFramework_RegisterAll() - 注册 Shell 命令模块
 *         3. 使用 Shell 命令查看: help, ls /, drivers
 */
int DrvFramework_RegisterAll(void)
{
    DBG("[DrvInit] Registering Shell command modules...\n");
    Shell_RegisterAllModules();
    DBG("[DrvInit] Shell command modules registered OK\n");
    return 0;
}

/**
 * @brief  注册平台设备驱动（SDIO SD 卡 / UART1、UART3）
 * @retval 0-成功, <0-失败
 *
 * @note   须在 DrvFramework_Init()（已创建 /driver 总线目录）之后调用。
 *         SDIO 设备 init 时检测 SD 卡并挂载 FatFs（f_mount），即接入 cfs。
 */
int DrvFramework_RegisterDevices(void)
{
    int ret;

    /* SDIO: 卡不在位或挂载失败时不注册（初始化成功才挂载到总线），
     * 此处仅告警，不影响 UART 设备注册 */
    ret = DrvSdio_Register();
    if (ret != 0) {
        DBG("[DrvInit] WARNING: SDIO device register failed (%d), "
            "SD card may not be present\n", ret);
    }

    ret = DrvUart_Register();
    if (ret != 0) {
        DBG("[DrvInit] UART device register failed (%d)\n", ret);
        return -2;
    }

    DBG("[DrvInit] Platform devices registered\n");
    return 0;
}

/**
 * @brief  驱动框架完整初始化（VFS 框架 + Shell 命令模块）
 * @retval 0-成功, <0-失败
 *
 * @note   在 main() 中调用此函数完成驱动框架初始化。
 *         随后需 Shell_Init()/Shell_SetIO() 并在主循环调用 Shell_Process()。
 */
int DrvFramework_FullInit(void)
{
    int ret;

    ret = DrvFramework_Init();
    if (ret != 0) {
        DBG("[DrvInit] WARNING: VFS init failed (%d)\n", ret);
        return ret;
    }

    ret = DrvFramework_RegisterAll();
    if (ret != 0) {
        DBG("[DrvInit] WARNING: Shell modules register failed (%d)\n", ret);
        return ret;
    }

    return 0;
}
