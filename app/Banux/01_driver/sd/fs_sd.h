/**
 *****************************************************************************
 * @file     fs_sd.h
 * @brief    SD 卡文件系统 -> VFS 挂载层
 *
 * 在 SDIO 驱动初始化成功（FatFs f_mount 成功）后，将 SD 卡文件系统
 * 挂载到 VFS 根目录下的 /sd 挂载点：
 *   /sd
 *       ├── gcode/            (目录, 进入时懒加载)
 *       │       ├── model.gco (文件, 支持 cat 读取内容)
 *       └── UPGRADE.BIN
 *
 * 挂载点使用懒加载：/sd 及子目录均为动态目录，ls/cd 时才通过
 * f_opendir/f_readdir 枚举真实文件系统，文件内容通过 f_open/f_read 读取。
 *****************************************************************************
 */
#ifndef __FS_SD_H__
#define __FS_SD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/**
 * @brief  将 SD 卡文件系统挂载到 VFS /sd
 * @return 0 成功；<0 失败（FATFS 未挂载或节点创建失败）
 * @note   仅在 DrvSdio_Register() 成功且 FatFs 已挂载时调用
 */
int  SdFs_Mount(void);

/** Mount an additional FatFs logical drive at a VFS root directory. */
int  FatFsVfs_Mount(const char *mountName, uint8_t drive);

/** Remove a logical-drive VFS mount. */
void FatFsVfs_Unmount(uint8_t drive);

/**
 * @brief  移除 /sd 挂载点（卸载文件系统）
 */
void SdFs_Unmount(void);

/** Create an empty SD file if it does not already exist. */
int SdFs_Touch(const char *vfsPath);

/** Replace an SD file with the supplied bytes. */
int SdFs_WriteFile(const char *vfsPath, const uint8_t *data, uint32_t len);

/** Write a chunk to a mounted FatFs file at the supplied byte offset. */
int SdFs_WriteFileAt(const char *vfsPath, const uint8_t *data, uint32_t len,
                     uint32_t offset);

/** Create one directory on the SD filesystem. */
int SdFs_Mkdir(const char *vfsPath);

/** Remove one SD file or an empty directory. */
int SdFs_Remove(const char *vfsPath);

#ifdef __cplusplus
}
#endif

#endif /* __FS_SD_H__ */
