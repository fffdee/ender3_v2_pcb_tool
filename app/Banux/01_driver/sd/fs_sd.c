/**
 *****************************************************************************
 * @file     fs_sd.c
 * @brief    SD 卡文件系统 -> VFS 挂载层实现
 *
 * 通过 dirLoad 懒加载回调把 FatFs 的目录树映射为 VFS 节点：
 *   - /sd          动态目录, 进入/ls 时枚举 SD 根目录
 *   - /sd/xxx      子目录同样为动态目录 (f_opendir/f_readdir)
 *   - /sd/xxx.txt  FILE 节点, cat 时经 fileRead 回调 f_open/f_read
 *
 * VFS 路径与 FatFs 路径的映射：/sd/a/b.txt -> 0:/a/b.txt
 *****************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "fs_sd.h"
#include "vfs.h"
#include "drv_fs.h"
#include "drv_sdio.h"
#include "fatfs.h"          /* SDFatFS / SDPath */
#include "ff.h"             /* f_opendir / f_readdir / f_open ... */
#include "debug.h"

#define FATFS_VFS_VOLUME_COUNT  2u

typedef struct {
    VfsNode_t *mountNode;
    uint8_t drive;
} FatFsVfsVolume_t;

static FatFsVfsVolume_t s_volumes[FATFS_VFS_VOLUME_COUNT];
static FIL s_file;
static DIR s_dir;
static FILINFO s_fileInfo;

static int is_valid_sfn(const char *name)
{
    static const char allowed[] = "!#$%&'()-@^_`{}~.";
    const unsigned char *p = (const unsigned char *)name;
    uint8_t len = 0u;

    if (!name || !name[0]) return 0;
    while (*p) {
        if (++len > 12u) return 0;
        if (!((*p >= '0' && *p <= '9') ||
              (*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') ||
              strchr(allowed, *p))) {
            return 0;
        }
        p++;
    }
    return 1;
}

static void log_invalid_sfn(const char *volumePath, const FILINFO *info)
{
    const uint8_t *name = (const uint8_t *)info->fname;
    DBG("[FatFsVfs] skipped corrupt directory entry: volume=%s "
        "attr=0x%02X name=%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
        volumePath, (unsigned int)info->fattrib,
        name[0], name[1], name[2], name[3], name[4], name[5], name[6],
        name[7], name[8], name[9], name[10], name[11], name[12]);
}

static FatFsVfsVolume_t *find_volume(VfsNode_t *node)
{
    uint8_t i;
    VfsNode_t *cur;

    for (i = 0u; i < FATFS_VFS_VOLUME_COUNT; i++) {
        cur = node;
        while (cur) {
            if (cur == s_volumes[i].mountNode) return &s_volumes[i];
            cur = cur->parent;
        }
    }
    return NULL;
}

static const char *path_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/*===========================================================================
 * 路径映射
 *===========================================================================*/
/**
 * @brief  将 VFS 节点路径映射为 FatFs 路径
 *         映射规则：/sd/a/b.txt -> 0:/a/b.txt
 */
static int build_fatfs_path(VfsNode_t *node, char *buf, uint16_t maxLen)
{
    char tmp[VFS_MAX_PATH_LEN];
    FatFsVfsVolume_t *volume;
    VfsNode_t *cur;
    int pos, len;

    if (!node || !buf || maxLen == 0) return -1;
    volume = find_volume(node);
    if (!volume) return -1;

    /* 从节点向上回溯, 逐段拼出相对挂载点的路径 */
    tmp[VFS_MAX_PATH_LEN - 1] = '\0';
    pos = VFS_MAX_PATH_LEN - 1;
    cur = node;
    while (cur && cur != volume->mountNode && cur->name[0] != '\0') {
        len = (int)strlen(cur->name);
        if (len <= 0) break;
        pos -= len;
        if (pos < 2) return -1;       /* 至少为卷前缀 "0:" 留位 */
        memcpy(&tmp[pos], cur->name, (size_t)len);
        pos--;
        tmp[pos] = '/';
        cur = cur->parent;
    }

    if (pos >= VFS_MAX_PATH_LEN - 1) {
        /* 节点就是挂载点本身 -> FatFs 根目录 */
        snprintf(buf, maxLen, "%u:/", (unsigned int)volume->drive);
        return 0;
    }

    /* 前缀卷号: pos 处已是一个 '/' */
    pos -= 2;
    if (pos < 0) return -1;
    tmp[pos]     = (char)('0' + volume->drive);
    tmp[pos + 1] = ':';
    strncpy(buf, &tmp[pos], maxLen - 1);
    buf[maxLen - 1] = '\0';
    return 0;
}

static int resolve_file_path(const char *vfsPath, VfsNode_t **parentOut,
                             char *fatPath, uint16_t fatPathLen)
{
    char path[VFS_MAX_PATH_LEN];
    char parentPath[VFS_MAX_PATH_LEN];
    char *slash;
    const char *name;
    VfsNode_t *parent;
    size_t len;

    if (!vfsPath || !parentOut || !fatPath || fatPathLen == 0) return -1;
    len = strlen(vfsPath);
    if (len == 0 || len >= sizeof(path)) return -1;
    memcpy(path, vfsPath, len + 1);

    slash = strrchr(path, '/');
    if (slash) {
        name = slash + 1;
        if (slash == path) {
            strcpy(parentPath, "/");
        } else {
            *slash = '\0';
            strncpy(parentPath, path, sizeof(parentPath) - 1);
            parentPath[sizeof(parentPath) - 1] = '\0';
        }
        parent = Vfs_FindNode(parentPath);
    } else {
        name = path;
        parent = Vfs_GetCwd();
    }

    if (!parent || (parent->type != VFS_NODE_DIR && parent->type != VFS_NODE_DEV) ||
        name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 ||
        strlen(name) >= VFS_MAX_NAME_LEN) {
        return -1;
    }

    if (!find_volume(parent)) return -1;

    if (build_fatfs_path(parent, fatPath, fatPathLen) != 0) return -1;
    len = strlen(fatPath);
    if (len > 0 && fatPath[len - 1] != '/') {
        if (len + 1 >= fatPathLen) return -1;
        fatPath[len++] = '/';
        fatPath[len] = '\0';
    }
    if (len + strlen(name) >= fatPathLen) return -1;
    strcat(fatPath, name);
    *parentOut = parent;
    return 0;
}

/*===========================================================================
 * 文件读取回调 (FILE 节点)
 *===========================================================================*/
static int sd_file_read(char *buf, uint16_t maxLen, uint32_t offset, void *userData)
{
    FRESULT fres;
    UINT br = 0;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *node = (VfsNode_t *)userData;

    if (!buf || maxLen == 0 || !node) return -1;
    if (build_fatfs_path(node, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_open(&s_file, path, FA_READ);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] f_open(%s) failed: %d\n", path, (int)fres);
        return -1;
    }
    if (offset > 0) {
        fres = f_lseek(&s_file, offset);
        if (fres != FR_OK) {
            f_close(&s_file);
            return -1;
        }
    }
    fres = f_read(&s_file, (void *)buf, maxLen, &br);
    f_close(&s_file);
    if (fres != FR_OK) {
        return -1;
    }
    return (int)br;
}

/*===========================================================================
 * 目录懒加载回调 (动态 DIR 节点)
 *===========================================================================*/
static int sd_dir_load(VfsNode_t *node, void *userData)
{
    FRESULT fres;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *child;

    (void)userData;
    if (!node) return -1;

    if (build_fatfs_path(node, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_opendir(&s_dir, path);
    if (fres != FR_OK) {
        DBG("[SdFs] f_opendir(%s) failed: %d\n", path, (int)fres);
        return -1;
    }

    /* 物理目录打开成功后再替换缓存，失败时保留原 VFS 子树。 */
    Vfs_ClearChildren(node);

    for (;;) {
        fres = f_readdir(&s_dir, &s_fileInfo);
        if (fres != FR_OK || s_fileInfo.fname[0] == '\0') {
            break;
        }
        /* 跳过卷标/隐藏条目 */
        if (s_fileInfo.fattrib & (AM_VOL | AM_HID)) {
            continue;
        }
        if (!is_valid_sfn(s_fileInfo.fname)) {
            log_invalid_sfn(path, &s_fileInfo);
            continue;
        }
        if (s_fileInfo.fattrib & AM_DIR) {
            child = Vfs_CreateNode(node, s_fileInfo.fname, VFS_NODE_DIR, NULL);
            if (!child) break;                       /* 子节点数/节点池耗尽 */
            child->dirLoad = sd_dir_load;            /* 子目录同样懒加载 */
        } else {
            child = Vfs_CreateFile(node, s_fileInfo.fname, s_fileInfo.fsize,
                                   sd_file_read, NULL);
            if (!child) break;
        }
    }
    f_closedir(&s_dir);
    return fres == FR_OK ? 0 : -1;
}

int SdFs_Touch(const char *vfsPath)
{
    FRESULT fres;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *parent;

    if (resolve_file_path(vfsPath, &parent, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_open(&s_file, path, FA_OPEN_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] touch open failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    fres = f_close(&s_file);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] touch close failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    if (!Vfs_FindNode(vfsPath) &&
        !Vfs_CreateFile(parent, path_name(vfsPath), 0u, sd_file_read, NULL)) {
        return -1;
    }
    return 0;
}

int SdFs_WriteFile(const char *vfsPath, const uint8_t *data, uint32_t len)
{
    FRESULT fres;
    UINT written = 0;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *parent;

    if ((!data && len > 0) ||
        resolve_file_path(vfsPath, &parent, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_open(&s_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] write open failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    if (len > 0) {
        fres = f_write(&s_file, data, (UINT)len, &written);
    }
    if (fres == FR_OK) fres = f_sync(&s_file);
    if (f_close(&s_file) != FR_OK && fres == FR_OK) fres = FR_DISK_ERR;
    if (fres != FR_OK || written != (UINT)len) {
        DBG("[FatFsVfs] write failed: path=%s result=%d bytes=%u/%lu\n",
            path, (int)fres, (unsigned int)written, (unsigned long)len);
        return fres != FR_OK ? -(int)fres : -(int)FR_DISK_ERR;
    }

    {
        VfsNode_t *node = Vfs_FindNode(vfsPath);
        if (!node) node = Vfs_CreateFile(parent, path_name(vfsPath), len,
                                         sd_file_read, NULL);
        if (!node) return -1;
        node->fileSize = len;
    }
    return 0;
}

int SdFs_WriteFileAt(const char *vfsPath, const uint8_t *data, uint32_t len,
                     uint32_t offset)
{
    FRESULT fres;
    UINT written = 0;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *parent;

    if ((!data && len > 0) ||
        resolve_file_path(vfsPath, &parent, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_open(&s_file, path, FA_OPEN_ALWAYS | FA_WRITE);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] write_at open failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    fres = f_lseek(&s_file, offset);
    if (fres == FR_OK && len > 0) {
        fres = f_write(&s_file, data, (UINT)len, &written);
    }
    if (fres == FR_OK) fres = f_sync(&s_file);
    if (f_close(&s_file) != FR_OK && fres == FR_OK) fres = FR_DISK_ERR;
    if (fres != FR_OK || written != (UINT)len) {
        DBG("[FatFsVfs] write_at failed: path=%s off=%lu result=%d bytes=%u/%lu\n",
            path, (unsigned long)offset, (int)fres,
            (unsigned int)written, (unsigned long)len);
        return fres != FR_OK ? -(int)fres : -(int)FR_DISK_ERR;
    }

    {
        uint32_t size = offset + len;
        VfsNode_t *node = Vfs_FindNode(vfsPath);
        if (!node) node = Vfs_CreateFile(parent, path_name(vfsPath), size,
                                         sd_file_read, NULL);
        if (!node) return -1;
        if (node->fileSize < size) node->fileSize = size;
    }
    return 0;
}

int SdFs_Mkdir(const char *vfsPath)
{
    FRESULT fres;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *parent;

    if (resolve_file_path(vfsPath, &parent, path, sizeof(path)) != 0) {
        return -1;
    }

    fres = f_mkdir(path);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] mkdir failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    {
        VfsNode_t *node = Vfs_CreateNode(parent, path_name(vfsPath),
                                         VFS_NODE_DIR, NULL);
        if (!node) return -1;
        node->dirLoad = sd_dir_load;
    }
    return 0;
}

int SdFs_Remove(const char *vfsPath)
{
    FRESULT fres;
    char path[VFS_MAX_PATH_LEN];
    VfsNode_t *parent;
    VfsNode_t *target;
    VfsNode_t *cur;

    if (resolve_file_path(vfsPath, &parent, path, sizeof(path)) != 0) {
        return -1;
    }

    target = Vfs_FindNode(vfsPath);
    if (target && target->type == VFS_NODE_DIR) {
        cur = Vfs_GetCwd();
        while (cur) {
            if (cur == target) return -(int)FR_DENIED;
            cur = cur->parent;
        }
    }

    fres = f_unlink(path);
    if (fres != FR_OK) {
        DBG("[FatFsVfs] remove failed: path=%s result=%d\n", path, (int)fres);
        return -(int)fres;
    }
    if (target) (void)Vfs_RemoveNode(target);
    return 0;
}

/*===========================================================================
 * 挂载 / 卸载
 *===========================================================================*/
int FatFsVfs_Mount(const char *mountName, uint8_t drive)
{
    FatFsVfsVolume_t *volume;

    if (!mountName || drive >= FATFS_VFS_VOLUME_COUNT) return -1;
    volume = &s_volumes[drive];
    if (volume->mountNode) return 0;

    volume->mountNode = Vfs_CreateDir(Vfs_GetRoot(), mountName);
    if (!volume->mountNode) {
        DBG("[FatFsVfs] ERROR: create /%s failed\n", mountName);
        return -2;
    }
    volume->drive = drive;
    volume->mountNode->dirLoad = sd_dir_load;
    Vfs_RefreshDir(volume->mountNode);
    DBG("[FatFsVfs] volume %u: mounted to /%s\n",
        (unsigned int)drive, mountName);
    return 0;
}

void FatFsVfs_Unmount(uint8_t drive)
{
    if (drive < FATFS_VFS_VOLUME_COUNT && s_volumes[drive].mountNode) {
        Vfs_RemoveNode(s_volumes[drive].mountNode);
        s_volumes[drive].mountNode = NULL;
        DBG("[FatFsVfs] volume %u unmounted\n", (unsigned int)drive);
    }
}

int SdFs_Mount(void)
{
    if (!DrvSdio_IsMounted()) {
        DBG("[SdFs] SD FATFS not mounted, skip VFS mount\n");
        return -1;
    }
    return FatFsVfs_Mount("sd", 0u);
}

void SdFs_Unmount(void)
{
    FatFsVfs_Unmount(0u);
}
