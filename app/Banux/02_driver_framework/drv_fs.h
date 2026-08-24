/**
 ******************************************************************************
 * @file    drv_fs.h
 * @brief   驱动文件系统适配层 - 基于VFS的驱动目录管理
 *
 * 提供 /driver 目录及子目录管理，驱动参数节点注册
 * 向后兼容API (DrvFs_* 映射到 Vfs_*)
 ******************************************************************************
 */
#ifndef __DRV_FS_H__
#define __DRV_FS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "vfs.h"

/*===========================================================================
 * 类型定义 (兼容旧API)
 *===========================================================================*/
typedef VfsNode_t         FsNode_t;
typedef VfsNodeType_t     FsNodeType_t;
typedef VfsError_t        FsError_t;
typedef VfsParamGet_t     FsParamGet_t;
typedef VfsParamSet_t     FsParamSet_t;
typedef VfsListCallback_t FsListCallback_t;

#define FS_NODE_DIR     VFS_NODE_DIR
#define FS_NODE_PARAM   VFS_NODE_PARAM
#define FS_NODE_DEV     VFS_NODE_DEV

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

/*===========================================================================
 * API兼容宏 (映射到VFS)
 *===========================================================================*/
#define DrvFs_GetRoot()             Vfs_GetRoot()
#define DrvFs_GetCwd()              Vfs_GetCwd()
#define DrvFs_GetCwdPath(b,l)       Vfs_GetCwdPath(b,l)
#define DrvFs_Cd(p)                 Vfs_Cd(p)
#define DrvFs_FindNode(p)           Vfs_FindNode(p)
#define DrvFs_CreateDir(p,n)        Vfs_CreateDir(p,n)
#define DrvFs_CreateParam(p,n,d,g,s,u)  Vfs_CreateParam(p,n,d,g,s,u)
#define DrvFs_CreateDevice(p,n,u)   Vfs_CreateDevice(p,n,u)
#define DrvFs_ReadParam(n,b,l)      Vfs_ReadParam(n,b,l)
#define DrvFs_WriteParam(n,v)       Vfs_WriteParam(n,v)
#define DrvFs_ListDir(n,c,u)        Vfs_ListDir(n,c,u)
#define DrvFs_RemoveNode(n)         Vfs_RemoveNode(n)
#define DrvFs_GetTypeName(t)        Vfs_GetTypeName(t)

/*===========================================================================
 * 驱动文件系统专用API
 *===========================================================================*/

/**
 * @brief  初始化驱动文件系统 (创建/driver及子目录)
 * @note   必须先调用 Vfs_Init()
 */
FsError_t DrvFs_Init(void);

FsNode_t* DrvFs_GetDriverDir(void);
FsNode_t* DrvFs_GetSpiDir(void);
FsNode_t* DrvFs_GetI2cDir(void);
FsNode_t* DrvFs_GetI2sDir(void);
FsNode_t* DrvFs_GetSdioDir(void);
FsNode_t* DrvFs_GetPowerDir(void);
FsNode_t* DrvFs_GetUsbDir(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_FS_H__ */
