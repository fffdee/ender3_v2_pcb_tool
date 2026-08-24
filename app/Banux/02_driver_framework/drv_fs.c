/**
 ******************************************************************************
 * @file    drv_fs.c
 * @brief   驱动文件系统适配层实现
 ******************************************************************************
 */
#include "drv_fs.h"

#if VFS_EN

static FsNode_t *g_DriverDir = NULL;
static FsNode_t *g_SpiDir = NULL;
static FsNode_t *g_I2cDir = NULL;
static FsNode_t *g_I2sDir = NULL;
static FsNode_t *g_SdioDir = NULL;
static FsNode_t *g_PowerDir = NULL;
static FsNode_t *g_UsbDir = NULL;
static bool      g_DrvFsInitialized = FALSE;

FsError_t DrvFs_Init(void)
{
    FsNode_t *root;

    if (g_DrvFsInitialized) return FS_OK;

    root = Vfs_GetRoot();
    if (!root) return FS_ERR_NOT_FOUND;

    g_DriverDir = Vfs_CreateDir(root, "driver");
    if (!g_DriverDir) return FS_ERR_NO_MEMORY;

    g_SpiDir   = Vfs_CreateDir(g_DriverDir, "spi");    if (!g_SpiDir)   return FS_ERR_NO_MEMORY;
    g_I2cDir   = Vfs_CreateDir(g_DriverDir, "i2c");    if (!g_I2cDir)   return FS_ERR_NO_MEMORY;
    g_I2sDir   = Vfs_CreateDir(g_DriverDir, "i2s");    if (!g_I2sDir)   return FS_ERR_NO_MEMORY;
    g_SdioDir  = Vfs_CreateDir(g_DriverDir, "sdio");   if (!g_SdioDir)  return FS_ERR_NO_MEMORY;
    g_PowerDir = Vfs_CreateDir(g_DriverDir, "power");  if (!g_PowerDir) return FS_ERR_NO_MEMORY;
    g_UsbDir   = Vfs_CreateDir(g_DriverDir, "usb");    if (!g_UsbDir)   return FS_ERR_NO_MEMORY;

    g_DrvFsInitialized = TRUE;
    return FS_OK;
}

FsNode_t* DrvFs_GetDriverDir(void) { return g_DriverDir; }
FsNode_t* DrvFs_GetSpiDir(void)    { return g_SpiDir; }
FsNode_t* DrvFs_GetI2cDir(void)    { return g_I2cDir; }
FsNode_t* DrvFs_GetI2sDir(void)    { return g_I2sDir; }
FsNode_t* DrvFs_GetSdioDir(void)   { return g_SdioDir; }
FsNode_t* DrvFs_GetPowerDir(void)  { return g_PowerDir; }
FsNode_t* DrvFs_GetUsbDir(void)    { return g_UsbDir; }

#endif /* VFS_EN */
