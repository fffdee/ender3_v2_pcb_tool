/**
 *****************************************************************************
 * @file     drv_fs.c
 * @author   BG Card Team
 * @version  V2.0.0
 * @date     04-January-2026
 * @brief    驱动文件系统适配层实现 - 创建/driver目录结构
 *****************************************************************************
 */

#include "drv_fs.h"
#include "debug.h"

/*******************************************************************************
 * 静态变量 - 驱动目录快捷访问指针
 ******************************************************************************/
static FsNode_t *g_DriverDir = NULL;
static FsNode_t *g_SpiDir = NULL;
static FsNode_t *g_I2cDir = NULL;
static FsNode_t *g_I2sDir = NULL;
static FsNode_t *g_SdioDir = NULL;
static FsNode_t *g_GpioDir = NULL;
static FsNode_t *g_UartDir = NULL;
static FsNode_t *g_PowerDir = NULL;
static FsNode_t *g_UsbDir = NULL;
static FsNode_t *g_TimerDir = NULL;
static bool      g_DrvFsInitialized = FALSE;

/*******************************************************************************
 * 内部工具
 ******************************************************************************/
/* 懒创建 /driver 下的总线子目录: 首次有设备注册时才创建对应目录,
 * 保证 /driver 下只存在"已初始化成功设备的总线目录"。 */
static FsNode_t* EnsureSubDir(FsNode_t **cache, const char *name)
{
    if (*cache) return *cache;
    if (!g_DriverDir) return NULL;
    *cache = Vfs_CreateDir(g_DriverDir, name);
    return *cache;
}

/*******************************************************************************
 * 公共API实现
 ******************************************************************************/

FsError_t DrvFs_Init(void)
{
    if (g_DrvFsInitialized) return FS_OK;
    
    FsNode_t *root = Vfs_GetRoot();
    if (!root) {
        DBG("[DrvFs] ERROR: VFS not initialized!\n");
        return FS_ERR_NOT_FOUND;
    }
    
    DBG("[DrvFs] Creating /driver directory...\n");
    
    /* 仅创建 /driver 根目录; 各总线子目录在设备注册时按需创建 */
    g_DriverDir = Vfs_CreateDir(root, "driver");
    if (!g_DriverDir) {
        DBG("[DrvFs] ERROR: Failed to create /driver\n");
        return FS_ERR_NO_MEMORY;
    }
    
    DBG("[DrvFs] /driver created (bus subdirs created on demand)\n");
    g_DrvFsInitialized = TRUE;
    return FS_OK;
}

FsNode_t* DrvFs_GetDriverDir(void)
{
    return g_DriverDir;
}

FsNode_t* DrvFs_GetSpiDir(void)
{
    return EnsureSubDir(&g_SpiDir, "spi");
}

FsNode_t* DrvFs_GetI2cDir(void)
{
    return EnsureSubDir(&g_I2cDir, "i2c");
}

FsNode_t* DrvFs_GetI2sDir(void)
{
    return EnsureSubDir(&g_I2sDir, "i2s");
}

FsNode_t* DrvFs_GetSdioDir(void)
{
    return EnsureSubDir(&g_SdioDir, "sdio");
}

FsNode_t* DrvFs_GetGpioDir(void)
{
    return EnsureSubDir(&g_GpioDir, "gpio");
}

FsNode_t* DrvFs_GetUartDir(void)
{
    return EnsureSubDir(&g_UartDir, "uart");
}

FsNode_t* DrvFs_GetPowerDir(void)
{
    return EnsureSubDir(&g_PowerDir, "power");
}

FsNode_t* DrvFs_GetUsbDir(void)
{
    return EnsureSubDir(&g_UsbDir, "usb");
}

FsNode_t* DrvFs_GetTimerDir(void)
{
    return EnsureSubDir(&g_TimerDir, "timer");
}
