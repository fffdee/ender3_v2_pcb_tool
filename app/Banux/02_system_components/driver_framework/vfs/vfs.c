/**
 ******************************************************************************
 * @file    vfs.c
 * @brief   虚拟文件系统核心实现
 *
 * 特点:
 *   - 静态内存池, 无 malloc
 *   - 路径解析支持绝对/相对路径, . 和 ..
 *   - 递归创建/删除节点
 ******************************************************************************
 */
#include <string.h>
#include "vfs.h"
#include "banux_component.h"

BANUX_COMPONENT_DEFINE(g_banux_component_vfs,
                       "vfs", "1.0.0", BANUX_COMPONENT_SYSTEM, VFS_EN,
                       "virtual filesystem core");

#if VFS_EN

/*===========================================================================
 * 静态变量
 *===========================================================================*/
static VfsNode_t g_NodePool[VFS_MAX_NODES];
static uint8_t   g_NodeUsed[VFS_MAX_NODES];
static uint8_t   g_NodeCount = 0;

static VfsNode_t *g_RootNode = NULL;
static VfsNode_t *g_CwdNode = NULL;
static bool       g_Initialized = FALSE;

/*===========================================================================
 * 内部函数声明
 *===========================================================================*/
static VfsNode_t* AllocNode(void);
static void FreeNode(VfsNode_t *node);
static void InitNode(VfsNode_t *node, const char *name, VfsNodeType_t type);
static VfsNode_t* FindChildByName(VfsNode_t *parent, const char *name);
static int ParsePath(const char *path, char segments[][VFS_MAX_NAME_LEN], int maxSegments);
static void BuildPath(VfsNode_t *node, char *buf, uint16_t maxLen);

/*===========================================================================
 * 内存管理
 *===========================================================================*/

static VfsNode_t* AllocNode(void)
{
    uint8_t i;
    for (i = 0; i < VFS_MAX_NODES; i++) {
        if (!g_NodeUsed[i]) {
            g_NodeUsed[i] = 1;
            g_NodeCount++;
            memset(&g_NodePool[i], 0, sizeof(VfsNode_t));
            return &g_NodePool[i];
        }
    }
    return NULL;
}

static void FreeNode(VfsNode_t *node)
{
    uint8_t i;
    if (!node) return;
    for (i = 0; i < VFS_MAX_NODES; i++) {
        if (&g_NodePool[i] == node && g_NodeUsed[i]) {
            g_NodeUsed[i] = 0;
            g_NodeCount--;
            return;
        }
    }
}

static void InitNode(VfsNode_t *node, const char *name, VfsNodeType_t type)
{
    if (!node) return;
    strncpy(node->name, name, VFS_MAX_NAME_LEN - 1);
    node->name[VFS_MAX_NAME_LEN - 1] = '\0';
    node->type = type;
    node->parent = NULL;
    node->childCount = 0;
    node->paramGet = NULL;
    node->paramSet = NULL;
    node->paramDesc = NULL;
    node->fileRead = NULL;
    node->fileSize = 0;
    node->dirLoad = NULL;
    node->dirLoaded = 0u;
    node->userData = NULL;
    node->driver = NULL;
    memset(node->children, 0, sizeof(node->children));
}

/*===========================================================================
 * 路径解析
 *===========================================================================*/

static int ParsePath(const char *path, char segments[][VFS_MAX_NAME_LEN], int maxSegments)
{
    int count = 0;
    const char *start = path;
    const char *p = path;
    int len;

    if (!path || !segments) return -1;

    while (*p == '/') p++;
    start = p;

    while (*p && count < maxSegments) {
        if (*p == '/') {
            len = (int)(p - start);
            if (len > 0 && len < VFS_MAX_NAME_LEN) {
                strncpy(segments[count], start, len);
                segments[count][len] = '\0';
                count++;
            }
            while (*p == '/') p++;
            start = p;
        } else {
            p++;
        }
    }

    if (start < p && count < maxSegments) {
        len = (int)(p - start);
        if (len > 0 && len < VFS_MAX_NAME_LEN) {
            strncpy(segments[count], start, len);
            segments[count][len] = '\0';
            count++;
        }
    }
    return count;
}

static VfsNode_t* FindChildByName(VfsNode_t *parent, const char *name)
{
    uint8_t i;
    if (!parent || !name) return NULL;
    for (i = 0; i < parent->childCount; i++) {
        if (parent->children[i] && strcmp(parent->children[i]->name, name) == 0)
            return parent->children[i];
    }
    return NULL;
}

static void BuildPath(VfsNode_t *node, char *buf, uint16_t maxLen)
{
    char tempPath[VFS_MAX_PATH_LEN];
    VfsNode_t *current;
    int pos, len;

    if (!node || !buf || maxLen == 0) return;
    if (node == g_RootNode) { strncpy(buf, "/", maxLen); return; }

    tempPath[VFS_MAX_PATH_LEN - 1] = '\0';
    pos = VFS_MAX_PATH_LEN - 1;
    current = node;
    while (current && current != g_RootNode) {
        len = (int)strlen(current->name);
        pos -= len;
        if (pos < 1) break;
        memcpy(&tempPath[pos], current->name, len);
        pos--;
        tempPath[pos] = '/';
        current = current->parent;
    }

    if (pos >= VFS_MAX_PATH_LEN - 1) {
        strncpy(buf, "/", maxLen);
    } else {
        strncpy(buf, &tempPath[pos], maxLen - 1);
        buf[maxLen - 1] = '\0';
    }
}

/*===========================================================================
 * 公共API实现
 *===========================================================================*/

VfsError_t Vfs_Init(void)
{
    if (g_Initialized) return VFS_OK;

    memset(g_NodePool, 0, sizeof(g_NodePool));
    memset(g_NodeUsed, 0, sizeof(g_NodeUsed));
    g_NodeCount = 0;

    g_RootNode = AllocNode();
    if (!g_RootNode) {
        BanuxComponent_SetState("vfs", BANUX_COMPONENT_FAILED);
        return VFS_ERR_NO_MEMORY;
    }

    InitNode(g_RootNode, "/", VFS_NODE_DIR);
    g_CwdNode = g_RootNode;
    g_Initialized = TRUE;
    BanuxComponent_SetState("vfs", BANUX_COMPONENT_READY);
    return VFS_OK;
}

VfsNode_t* Vfs_GetRoot(void) { return g_RootNode; }
VfsNode_t* Vfs_GetCwd(void) { return g_CwdNode; }

VfsError_t Vfs_GetCwdPath(char *buf, uint16_t maxLen)
{
    if (!buf || maxLen == 0) return VFS_ERR_INVALID_PATH;
    if (!g_CwdNode) return VFS_ERR_NOT_FOUND;
    BuildPath(g_CwdNode, buf, maxLen);
    return VFS_OK;
}

VfsError_t Vfs_Cd(const char *path)
{
    VfsNode_t *target;
    char segments[8][VFS_MAX_NAME_LEN];
    int segCount, i;
    VfsNode_t *current;

    if (!path) return VFS_ERR_INVALID_PATH;
    if (path[0] == '\0' || (path[0] == '/' && path[1] == '\0')) {
        g_CwdNode = g_RootNode;
        return VFS_OK;
    }

    segCount = ParsePath(path, segments, 8);
    if (segCount < 0) return VFS_ERR_INVALID_PATH;

    current = (path[0] == '/') ? g_RootNode : g_CwdNode;

    for (i = 0; i < segCount; i++) {
        if (strcmp(segments[i], "..") == 0) {
            if (current->parent) current = current->parent;
        } else if (strcmp(segments[i], ".") == 0) {
            /* 当前目录 */
        } else {
            /* 动态目录 (挂载点) 进入前先懒加载子节点 */
            if (current->dirLoad && !current->dirLoaded) {
                Vfs_RefreshDir(current);
            }
            target = FindChildByName(current, segments[i]);
            if (!target) return VFS_ERR_NOT_FOUND;
            if (target->type != VFS_NODE_DIR && target->type != VFS_NODE_DEV)
                return VFS_ERR_NOT_DIR;
            current = target;
        }
    }
    g_CwdNode = current;
    return VFS_OK;
}

VfsNode_t* Vfs_FindNode(const char *path)
{
    char segments[8][VFS_MAX_NAME_LEN];
    int segCount, i;
    VfsNode_t *current, *target;

    if (!path) return NULL;
    if (path[0] == '/' && path[1] == '\0') return g_RootNode;

    segCount = ParsePath(path, segments, 8);
    if (segCount < 0) return NULL;

    current = (path[0] == '/') ? g_RootNode : g_CwdNode;

    for (i = 0; i < segCount; i++) {
        if (strcmp(segments[i], "..") == 0) {
            if (current->parent) current = current->parent;
        } else if (strcmp(segments[i], ".") == 0) {
            /* 当前目录 */
        } else {
            /* 动态目录 (挂载点) 进入前先懒加载子节点 */
            if (current->dirLoad && !current->dirLoaded) {
                Vfs_RefreshDir(current);
            }
            target = FindChildByName(current, segments[i]);
            if (!target) return NULL;
            current = target;
        }
    }
    return current;
}

VfsNode_t* Vfs_CreateDir(VfsNode_t *parent, const char *name)
{
    VfsNode_t *node;
    if (!parent || !name) return NULL;
    if (parent->type != VFS_NODE_DIR && parent->type != VFS_NODE_DEV) return NULL;
    if (strlen(name) >= VFS_MAX_NAME_LEN) return NULL;
    if (parent->childCount >= VFS_MAX_CHILDREN) return NULL;
    if (FindChildByName(parent, name)) return NULL;

    node = AllocNode();
    if (!node) return NULL;
    InitNode(node, name, VFS_NODE_DIR);
    node->parent = parent;
    parent->children[parent->childCount++] = node;
    return node;
}

VfsNode_t* Vfs_Mkdir(const char *path)
{
    char segments[8][VFS_MAX_NAME_LEN];
    int segCount, i;
    VfsNode_t *current, *child;

    if (!path || path[0] != '/') return NULL;
    segCount = ParsePath(path, segments, 8);
    if (segCount < 0) return NULL;

    current = g_RootNode;
    for (i = 0; i < segCount; i++) {
        child = FindChildByName(current, segments[i]);
        if (child) {
            if (child->type != VFS_NODE_DIR) return NULL;
            current = child;
        } else {
            current = Vfs_CreateDir(current, segments[i]);
            if (!current) return NULL;
        }
    }
    return current;
}

VfsNode_t* Vfs_CreateParam(VfsNode_t *parent, const char *name,
                            const char *desc,
                            VfsParamGet_t get, VfsParamSet_t set,
                            void *userData)
{
    VfsNode_t *node;
    if (!parent || !name || (!get && !set)) return NULL;
    if (parent->type != VFS_NODE_DIR && parent->type != VFS_NODE_DEV) return NULL;
    if (strlen(name) >= VFS_MAX_NAME_LEN) return NULL;
    if (parent->childCount >= VFS_MAX_CHILDREN) return NULL;
    if (FindChildByName(parent, name)) return NULL;

    node = AllocNode();
    if (!node) return NULL;
    InitNode(node, name, VFS_NODE_PARAM);
    node->parent = parent;
    node->paramGet = get;
    node->paramSet = set;
    node->paramDesc = desc;
    node->userData = userData;
    parent->children[parent->childCount++] = node;
    return node;
}

VfsNode_t* Vfs_CreateDevice(VfsNode_t *parent, const char *name, void *userData)
{
    VfsNode_t *node;
    if (!parent || !name) return NULL;
    if (parent->type != VFS_NODE_DIR) return NULL;
    if (strlen(name) >= VFS_MAX_NAME_LEN) return NULL;
    if (parent->childCount >= VFS_MAX_CHILDREN) return NULL;
    if (FindChildByName(parent, name)) return NULL;

    node = AllocNode();
    if (!node) return NULL;
    InitNode(node, name, VFS_NODE_DEV);
    node->parent = parent;
    node->userData = userData;
    parent->children[parent->childCount++] = node;
    return node;
}

VfsNode_t* Vfs_CreateNode(VfsNode_t *parent, const char *name,
                           VfsNodeType_t type, void *userData)
{
    VfsNode_t *node;
    if (!parent || !name) return NULL;
    if (parent->childCount >= VFS_MAX_CHILDREN) return NULL;

    node = AllocNode();
    if (!node) return NULL;
    InitNode(node, name, type);
    node->parent = parent;
    node->userData = userData;
    parent->children[parent->childCount++] = node;
    return node;
}

VfsNode_t* Vfs_CreateFile(VfsNode_t *parent, const char *name,
                          uint32_t size, VfsFileRead_t read, void *userData)
{
    VfsNode_t *node;
    if (!parent || !name || !read) return NULL;
    if (parent->type != VFS_NODE_DIR && parent->type != VFS_NODE_DEV) return NULL;
    if (strlen(name) >= VFS_MAX_NAME_LEN) return NULL;
    if (parent->childCount >= VFS_MAX_CHILDREN) return NULL;
    if (FindChildByName(parent, name)) return NULL;

    node = AllocNode();
    if (!node) return NULL;
    InitNode(node, name, VFS_NODE_FILE);
    node->parent = parent;
    node->fileRead = read;
    node->fileSize = size;
    node->userData = userData;
    parent->children[parent->childCount++] = node;
    return node;
}

int Vfs_ReadFile(VfsNode_t *node, char *buf, uint16_t maxLen, uint32_t offset)
{
    if (!node || !buf || maxLen == 0) return -1;
    if (node->type == VFS_NODE_PARAM) {
        /* 兼容参数节点: 直接走参数读取 */
        return Vfs_ReadParam(node, buf, maxLen);
    }
    if (node->type != VFS_NODE_FILE) return -1;
    if (!node->fileRead) return -2;
    /* 将节点指针作为 userData 传入, 便于回调定位真实文件 */
    return node->fileRead(buf, maxLen, offset, node);
}

VfsError_t Vfs_SetDirLoader(VfsNode_t *dirNode, VfsDirLoad_t loader, void *userData)
{
    if (!dirNode) return VFS_ERR_INVALID_PATH;
    if (dirNode->type != VFS_NODE_DIR && dirNode->type != VFS_NODE_DEV)
        return VFS_ERR_NOT_DIR;
    dirNode->dirLoad = loader;
    dirNode->userData = userData;
    return VFS_OK;
}

VfsError_t Vfs_RefreshDir(VfsNode_t *node)
{
    int ret;
    if (!node) return VFS_ERR_INVALID_PATH;
    if (node->type != VFS_NODE_DIR && node->type != VFS_NODE_DEV)
        return VFS_ERR_NOT_DIR;
    if (!node->dirLoad) return VFS_OK;   /* 静态目录无需刷新 */
    ret = node->dirLoad(node, node->userData);
    if (ret == 0) node->dirLoaded = 1u;
    return (ret == 0) ? VFS_OK : VFS_ERR_NOT_FOUND;
}

VfsError_t Vfs_ClearChildren(VfsNode_t *node)
{
    if (!node) return VFS_ERR_INVALID_PATH;
    while (node->childCount > 0) {
        Vfs_RemoveNode(node->children[0]);
    }
    return VFS_OK;
}

int Vfs_ReadParam(VfsNode_t *node, char *buf, uint16_t maxLen)
{
    if (!node || !buf || maxLen == 0) return -1;
    if (node->type != VFS_NODE_PARAM) return -1;
    if (!node->paramGet) return -2;
    return node->paramGet(buf, maxLen, node->userData);
}

VfsError_t Vfs_WriteParam(VfsNode_t *node, const char *value)
{
    if (!node || !value) return VFS_ERR_INVALID_PATH;
    if (node->type != VFS_NODE_PARAM) return VFS_ERR_NOT_PARAM;
    if (!node->paramSet) return VFS_ERR_READ_ONLY;
    return (node->paramSet(value, node->userData) == 0) ? VFS_OK : VFS_ERR_INVALID_PATH;
}

VfsError_t Vfs_ListDir(VfsNode_t *node, VfsListCallback_t callback, void *userData)
{
    uint8_t i;
    if (!node || !callback) return VFS_ERR_INVALID_PATH;
    if (node->type != VFS_NODE_DIR && node->type != VFS_NODE_DEV)
        return VFS_ERR_NOT_DIR;
    /* 动态目录先懒加载子节点 */
    if (node->dirLoad && !node->dirLoaded) {
        Vfs_RefreshDir(node);
    }
    for (i = 0; i < node->childCount; i++) {
        if (node->children[i])
            callback(node->children[i], userData);
    }
    return VFS_OK;
}

const char* Vfs_GetTypeName(VfsNodeType_t type)
{
    switch (type) {
        case VFS_NODE_DIR:   return "DIR";
        case VFS_NODE_PARAM: return "PARAM";
        case VFS_NODE_DEV:   return "DEV";
        case VFS_NODE_CMD:   return "CMD";
        case VFS_NODE_FILE:  return "FILE";
        default:             return "???";
    }
}

VfsError_t Vfs_RemoveNode(VfsNode_t *node)
{
    uint8_t i, j;
    VfsNode_t *parent;

    if (!node) return VFS_ERR_NOT_FOUND;
    if (node == g_RootNode) return VFS_ERR_INVALID_PATH;

    /* childCount 会在删除时递减，始终删除第一个子节点。 */
    while (node->childCount > 0)
        Vfs_RemoveNode(node->children[0]);

    parent = node->parent;
    if (parent) {
        for (i = 0; i < parent->childCount; i++) {
            if (parent->children[i] == node) {
                for (j = i; j < parent->childCount - 1; j++)
                    parent->children[j] = parent->children[j + 1];
                parent->children[parent->childCount - 1] = NULL;
                parent->childCount--;
                break;
            }
        }
    }

    if (g_CwdNode == node)
        g_CwdNode = parent ? parent : g_RootNode;

    FreeNode(node);
    return VFS_OK;
}

#endif /* VFS_EN */
