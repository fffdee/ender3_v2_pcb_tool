/**
 *****************************************************************************
 * @file     drv_fs.h
 * @author   BG Card Team
 * @version  V2.0.0
 * @date     04-January-2026
 * @brief    驱动文件系统适配层 - 基于VFS的驱动目录管理
 *****************************************************************************
 * @attention
 *
 * 本模块是VFS的驱动层适配，提供：
 * 1. /driver 目录及子目录管理
 * 2. 驱动参数节点注册
 * 3. 向后兼容的API（DrvFs_* 映射到 Vfs_*）
 *
 * 目录结构：
 *   /driver
 *       ├── spi
 *       │   ├── st7735
 *       │   └── w25q64
 *       ├── i2c
 *       ├── i2s
 *       ├── sdio
 *       ├── uart
 *       ├── power
 *       └── usb
 *
 *****************************************************************************
 */

#ifndef __DRV_FS_H__
#define __DRV_FS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "vfs.h"

/*******************************************************************************
 * 类型定义（兼容旧API）
 ******************************************************************************/
typedef VfsNode_t       FsNode_t;
typedef VfsNodeType_t   FsNodeType_t;
typedef VfsError_t      FsError_t;
typedef VfsParamGet_t   FsParamGet_t;
typedef VfsParamSet_t   FsParamSet_t;
typedef VfsListCallback_t FsListCallback_t;
typedef VfsFileRead_t   FsFileRead_t;
typedef VfsDirLoad_t    FsDirLoad_t;

/* 节点类型兼容定义 */
#define FS_NODE_DIR     VFS_NODE_DIR
#define FS_NODE_PARAM   VFS_NODE_PARAM
#define FS_NODE_DEV     VFS_NODE_DEV
#define FS_NODE_CMD     VFS_NODE_CMD
#define FS_NODE_FILE    VFS_NODE_FILE

/* 错误码兼容定义 */
#define FS_OK                   VFS_OK
#define FS_ERR_NOT_FOUND        VFS_ERR_NOT_FOUND
#define FS_ERR_NOT_DIR          VFS_ERR_NOT_DIR
#define FS_ERR_NOT_PARAM        VFS_ERR_NOT_PARAM
#define FS_ERR_READ_ONLY        VFS_ERR_READ_ONLY
#define FS_ERR_NO_MEMORY        VFS_ERR_NO_MEMORY
#define FS_ERR_NAME_TOO_LONG    VFS_ERR_NAME_TOO_LONG
#define FS_ERR_DIR_FULL         VFS_ERR_DIR_FULL
#define FS_ERR_ALREADY_EXISTS   VFS_ERR_ALREADY_EXISTS
#define FS_ERR_INVALID_PATH     VFS_ERR_INVALID_PATH

/* 配置兼容定义 */
#define DRV_FS_MAX_PATH_LEN     VFS_MAX_PATH_LEN
#define DRV_FS_MAX_NAME_LEN     VFS_MAX_NAME_LEN
#define DRV_FS_MAX_CHILDREN     VFS_MAX_CHILDREN
#define DRV_FS_MAX_PARAM_LEN    VFS_MAX_PARAM_LEN
#define DRV_FS_MAX_NODES        VFS_MAX_NODES

/*******************************************************************************
 * API兼容宏（映射到VFS）
 ******************************************************************************/
#define DrvFs_GetRoot()             Vfs_GetRoot()
#define DrvFs_GetCwd()              Vfs_GetCwd()
#define DrvFs_GetCwdPath(b,l)       Vfs_GetCwdPath(b,l)
#define DrvFs_Cd(p)                 Vfs_Cd(p)
#define DrvFs_FindNode(p)           Vfs_FindNode(p)
#define DrvFs_CreateDir(p,n)        Vfs_CreateDir(p,n)
#define DrvFs_CreateParam(p,n,d,g,s,u)  Vfs_CreateParam(p,n,d,g,s,u)
#define DrvFs_CreateDevice(p,n,u)   Vfs_CreateDevice(p,n,u)
#define DrvFs_CreateFile(p,n,s,r,u) Vfs_CreateFile(p,n,s,r,u)
#define DrvFs_ReadFile(n,b,l,o)     Vfs_ReadFile(n,b,l,o)
#define DrvFs_SetDirLoader(n,l,u)   Vfs_SetDirLoader(n,l,u)
#define DrvFs_RefreshDir(n)         Vfs_RefreshDir(n)
#define DrvFs_ClearChildren(n)      Vfs_ClearChildren(n)
#define DrvFs_ReadParam(n,b,l)      Vfs_ReadParam(n,b,l)
#define DrvFs_WriteParam(n,v)       Vfs_WriteParam(n,v)
#define DrvFs_ListDir(n,c,u)        Vfs_ListDir(n,c,u)
#define DrvFs_RemoveNode(n)         Vfs_RemoveNode(n)
#define DrvFs_GetTypeName(t)        Vfs_GetTypeName(t)

/*******************************************************************************
 * 驱动文件系统专用API
 ******************************************************************************/

/**
 * @brief  初始化驱动文件系统（创建/driver及子目录）
 * @return FS_OK成功，其他失败
 * @note   必须先调用Vfs_Init()
 */
FsError_t DrvFs_Init(void);

/**
 * @brief  获取/driver目录节点
 */
FsNode_t* DrvFs_GetDriverDir(void);

/**
 * @brief  获取/driver/spi目录节点
 */
FsNode_t* DrvFs_GetSpiDir(void);

/**
 * @brief  获取/driver/i2c目录节点
 */
FsNode_t* DrvFs_GetI2cDir(void);

/**
 * @brief  获取/driver/i2s目录节点
 */
FsNode_t* DrvFs_GetI2sDir(void);

/**
 * @brief  获取/driver/sdio目录节点
 */
FsNode_t* DrvFs_GetSdioDir(void);

/** Get or create /driver/gpio. */
FsNode_t* DrvFs_GetGpioDir(void);

/**
 * @brief  获取/driver/uart目录节点
 */
FsNode_t* DrvFs_GetUartDir(void);

/**
 * @brief  获取/driver/power目录节点
 */
FsNode_t* DrvFs_GetPowerDir(void);

/**
 * @brief  获取/driver/usb目录节点
 */
FsNode_t* DrvFs_GetUsbDir(void);

/** Get or create /driver/timer. */
FsNode_t* DrvFs_GetTimerDir(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_FS_H__ */
