/**
 ******************************************************************************
 * @file    vfs.h
 * @brief   虚拟文件系统 - 类Linux树形目录结构
 *
 * 实现:
 *   1. 树形目录结构 (driver/spi/st7735/param1, /bin/sys/info)
 *   2. 节点类型: 目录(DIR) / 参数(PARAM) / 设备(DEV) / 命令(CMD)
 *   3. 路径解析与导航 (cd, ls, find)
 *   4. 与Shell命令系统绑定
 *
 * 目录结构示例:
 *   /
 *   ├── bin                    # 系统命令
 *   │   └── sys
 *   │       ├── info
 *   │       ├── mem
 *   │       └── tasks
 *   └── driver                 # 硬件驱动
 *       ├── spi
 *       │   ├── st7735
 *       │   │   ├── name
 *       │   │   ├── width
 *       │   │   └── height
 *       │   └── w25q64
 *       ├── i2c
 *       ├── i2s
 *       └── usb
 ******************************************************************************
 */
#ifndef __VFS_H__
#define __VFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "banux_config.h"

/*===========================================================================
 * 节点类型定义
 *===========================================================================*/
typedef enum {
    VFS_NODE_DIR = 0,        /* 目录节点 */
    VFS_NODE_PARAM,          /* 参数节点 (可读写) */
    VFS_NODE_DEV,            /* 设备节点 (关联驱动) */
    VFS_NODE_CMD,            /* 命令节点 (/bin命令) */
    VFS_NODE_FILE,           /* 文件节点 (真实文件系统如 SD 卡中的文件) */
} VfsNodeType_t;

/*===========================================================================
 * 节点结构前向声明 (供回调函数类型使用)
 *===========================================================================*/
typedef struct VfsNode VfsNode_t;

/*===========================================================================
 * 参数读写回调函数类型
 *===========================================================================*/

/**
 * @brief  参数读取回调
 * @param  buf: 输出缓冲区
 * @param  maxLen: 缓冲区最大长度
 * @param  userData: 用户数据 (设备私有数据)
 * @return 实际读取的长度, -1表示错误
 */
typedef int (*VfsParamGet_t)(char *buf, uint16_t maxLen, void *userData);

/**
 * @brief  参数写入回调
 * @param  value: 写入的值字符串
 * @param  userData: 用户数据
 * @return 0成功, -1失败
 */
typedef int (*VfsParamSet_t)(const char *value, void *userData);

/**
 * @brief  文件内容读取回调 (FILE 节点)
 * @param  buf: 输出缓冲区
 * @param  maxLen: 缓冲区最大长度
 * @param  offset: 文件读取偏移
 * @param  userData: 用户数据 (通常为节点指针)
 * @return 实际读取的字节数, -1表示错误
 */
typedef int (*VfsFileRead_t)(char *buf, uint16_t maxLen, uint32_t offset, void *userData);

/**
 * @brief  动态目录子节点枚举回调 (挂载真实文件系统用)
 *         回调内应通过 Vfs_ClearChildren 清空旧子节点后重新枚举填充。
 * @param  node: 目标目录节点
 * @param  userData: 用户数据
 * @return 0成功, 非0失败
 */
typedef int (*VfsDirLoad_t)(VfsNode_t *node, void *userData);

/*===========================================================================
 * 文件系统节点结构 (树形结构)
 *===========================================================================*/
typedef struct VfsNode {
    char                name[VFS_MAX_NAME_LEN];      /* 节点名称 */
    VfsNodeType_t       type;                        /* 节点类型 */
    struct VfsNode     *parent;                      /* 父节点 */
    struct VfsNode     *children[VFS_MAX_CHILDREN];  /* 子节点数组 */
    uint8_t             childCount;                  /* 子节点数量 */

    /* 参数节点专用 */
    VfsParamGet_t       paramGet;                    /* 参数读取函数 */
    VfsParamSet_t       paramSet;                    /* 参数写入函数 */
    const char         *paramDesc;                   /* 参数描述 */

    /* 文件节点专用 */
    VfsFileRead_t       fileRead;                    /* 文件内容读取回调 */
    uint32_t            fileSize;                    /* 文件大小 */

    /* 动态目录专用 (挂载真实文件系统, 进入时懒加载子节点) */
    VfsDirLoad_t        dirLoad;                     /* 目录子节点枚举回调 */
    uint8_t             dirLoaded;                   /* 已完成至少一次成功枚举 */

    /* 设备/参数节点专用 */
    void               *userData;                    /* 用户私有数据 */
    void               *driver;                      /* 关联的驱动指针 */
} VfsNode_t;

/*===========================================================================
 * 错误码定义
 *===========================================================================*/
typedef enum {
    VFS_OK = 0,              /* 成功 */
    VFS_ERR_NOT_FOUND,       /* 路径不存在 */
    VFS_ERR_NOT_DIR,         /* 不是目录 */
    VFS_ERR_NOT_PARAM,       /* 不是参数节点 */
    VFS_ERR_READ_ONLY,       /* 参数只读 */
    VFS_ERR_NO_MEMORY,       /* 内存不足 */
    VFS_ERR_NAME_TOO_LONG,   /* 名称过长 */
    VFS_ERR_DIR_FULL,        /* 目录已满 */
    VFS_ERR_ALREADY_EXISTS,  /* 节点已存在 */
    VFS_ERR_INVALID_PATH,    /* 无效路径 */
} VfsError_t;

/*===========================================================================
 * 目录列举回调函数类型
 *===========================================================================*/
typedef void (*VfsListCallback_t)(VfsNode_t *node, void *userData);

/*===========================================================================
 * 参数定义结构体 (用于批量创建参数)
 *===========================================================================*/
typedef struct {
    const char *name;
    const char *desc;
    int (*get)(char *buf, uint16_t maxLen, void *userData);
    int (*set)(const char *buf, void *userData);
    void *userData;
} FsParamDef_t;

#define FS_PARAM_END {NULL, NULL, NULL, NULL, NULL}
#define FS_PARAM_DEF(n, d, g, s) {(n), (d), (g), (s), NULL}

/*===========================================================================
 * 核心API
 *===========================================================================*/

/**
 * @brief  初始化虚拟文件系统 (创建根节点)
 * @return VFS_OK成功
 */
VfsError_t Vfs_Init(void);

/**
 * @brief  获取根节点
 */
VfsNode_t* Vfs_GetRoot(void);

/**
 * @brief  获取当前工作目录节点
 */
VfsNode_t* Vfs_GetCwd(void);

/**
 * @brief  获取当前工作目录路径
 */
VfsError_t Vfs_GetCwdPath(char *buf, uint16_t maxLen);

/**
 * @brief  切换当前目录
 * @param  path: 目标路径 (支持相对/绝对路径, . 和 ..)
 */
VfsError_t Vfs_Cd(const char *path);

/**
 * @brief  根据路径查找节点
 * @return 节点指针, NULL=未找到
 */
VfsNode_t* Vfs_FindNode(const char *path);

/**
 * @brief  在指定目录下创建子目录
 */
VfsNode_t* Vfs_CreateDir(VfsNode_t *parent, const char *name);

/**
 * @brief  递归创建目录 (如 "/bin/sys")
 */
VfsNode_t* Vfs_Mkdir(const char *path);

/**
 * @brief  创建参数节点
 * @param  get: 读取回调 (NULL表示只写)
 * @param  set: 写入回调 (NULL表示只读)
 */
VfsNode_t* Vfs_CreateParam(VfsNode_t *parent, const char *name,
                            const char *desc,
                            VfsParamGet_t get, VfsParamSet_t set,
                            void *userData);

/**
 * @brief  创建设备节点
 */
VfsNode_t* Vfs_CreateDevice(VfsNode_t *parent, const char *name, void *userData);

/**
 * @brief  创建通用节点
 */
VfsNode_t* Vfs_CreateNode(VfsNode_t *parent, const char *name,
                           VfsNodeType_t type, void *userData);

/**
 * @brief  创建文件节点 (挂载真实文件系统用)
 * @param  size: 文件大小
 * @param  read: 文件内容读取回调 (不可为 NULL)
 */
VfsNode_t* Vfs_CreateFile(VfsNode_t *parent, const char *name,
                          uint32_t size, VfsFileRead_t read, void *userData);

/**
 * @brief  读取文件内容 (FILE 节点)
 * @return 实际读取的字节数, -1错误
 */
int Vfs_ReadFile(VfsNode_t *node, char *buf, uint16_t maxLen, uint32_t offset);

/**
 * @brief  为目录节点设置懒加载回调 (挂载真实文件系统)
 */
VfsError_t Vfs_SetDirLoader(VfsNode_t *dirNode, VfsDirLoad_t loader, void *userData);

/**
 * @brief  刷新动态目录: 触发 dirLoad 重新枚举子节点 (静态目录直接返回 OK)
 */
VfsError_t Vfs_RefreshDir(VfsNode_t *node);

/**
 * @brief  清空某节点的全部子节点 (供 dirLoad 刷新重建用)
 */
VfsError_t Vfs_ClearChildren(VfsNode_t *node);

/**
 * @brief  读取参数值
 * @return 读取的字节数, -1错误, -2只写参数
 */
int Vfs_ReadParam(VfsNode_t *node, char *buf, uint16_t maxLen);

/**
 * @brief  写入参数值
 */
VfsError_t Vfs_WriteParam(VfsNode_t *node, const char *value);

/**
 * @brief  列举目录内容
 */
VfsError_t Vfs_ListDir(VfsNode_t *node, VfsListCallback_t callback, void *userData);

/**
 * @brief  删除节点 (递归删除子节点)
 */
VfsError_t Vfs_RemoveNode(VfsNode_t *node);

/**
 * @brief  获取节点类型名称
 */
const char* Vfs_GetTypeName(VfsNodeType_t type);

/*===========================================================================
 * VFS_EN=0 时的空操作宏 (调用方无需修改)
 *===========================================================================*/
#if !VFS_EN
#define Vfs_Init()                                      (VFS_OK)
#define Vfs_GetRoot()                                   ((VfsNode_t*)NULL)
#define Vfs_GetCwd()                                    ((VfsNode_t*)NULL)
#define Vfs_GetCwdPath(buf, maxLen)                     (VFS_OK)
#define Vfs_Cd(path)                                    (VFS_ERR_NOT_FOUND)
#define Vfs_FindNode(path)                              ((VfsNode_t*)NULL)
#define Vfs_CreateDir(parent, name)                     ((VfsNode_t*)NULL)
#define Vfs_Mkdir(path)                                 ((VfsNode_t*)NULL)
#define Vfs_CreateParam(par, name, desc, get, set, ud)  ((VfsNode_t*)NULL)
#define Vfs_CreateDevice(parent, name, ud)              ((VfsNode_t*)NULL)
#define Vfs_CreateNode(par, name, type, ud)             ((VfsNode_t*)NULL)
#define Vfs_CreateFile(par, name, size, read, ud)       ((VfsNode_t*)NULL)
#define Vfs_ReadFile(node, buf, maxLen, offset)         (-1)
#define Vfs_SetDirLoader(node, loader, ud)              (VFS_ERR_NOT_FOUND)
#define Vfs_RefreshDir(node)                            (VFS_OK)
#define Vfs_ClearChildren(node)                         (VFS_OK)
#define Vfs_ReadParam(node, buf, maxLen)                (-1)
#define Vfs_WriteParam(node, value)                     (VFS_ERR_NOT_FOUND)
#define Vfs_ListDir(node, cb, ud)                       (VFS_OK)
#define Vfs_RemoveNode(node)                            (VFS_OK)
#define Vfs_GetTypeName(type)                           ("")
#endif /* !VFS_EN */

#ifdef __cplusplus
}
#endif

#endif /* __VFS_H__ */
