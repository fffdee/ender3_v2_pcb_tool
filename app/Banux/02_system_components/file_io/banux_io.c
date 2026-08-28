/** @file banux_io.c @brief Path-based VFS driver access implementation. */
#include <string.h>
#include "banux_config.h"
#include "banux_io.h"
#include "banux_component.h"
#include "vfs.h"
#include "drv_device.h"

BANUX_COMPONENT_DEFINE(g_banux_component_file_io,
                       "file_io", "1.0.0", BANUX_COMPONENT_SYSTEM,
                       BANUX_IO_EN, "standard path-based read/write system");

void BanuxIo_Init(void)
{
#if BANUX_IO_EN
    BanuxComponent_SetState("file_io", Vfs_GetRoot()
                            ? BANUX_COMPONENT_READY
                            : BANUX_COMPONENT_FAILED);
#endif
}

#if BANUX_IO_EN
static VfsNode_t *banux_io_find(const char *path)
{
    if (!path || path[0] == '\0') return NULL;
    return Vfs_FindNode(path);
}

static DrvDevice_t *banux_io_device(VfsNode_t *node)
{
    if (!node || node->type != VFS_NODE_DEV || !node->driver) return NULL;
    return (DrvDevice_t *)node->driver;
}
#endif

int banux_open(const char *path)
{
#if BANUX_IO_EN
    VfsNode_t *node = banux_io_find(path);
    DrvDevice_t *device;
    int ret;

    if (!node) return BANUX_IO_ERR_NOT_FOUND;
    device = banux_io_device(node);
    if (!device) return BANUX_IO_ERR_WRONG_TYPE;
    if (device->isOpened) return BANUX_IO_OK;
    ret = device->open ? device->open(device->privData) : 0;
    if (ret != 0) return BANUX_IO_ERR_DRIVER;
    device->isOpened = TRUE;
    return BANUX_IO_OK;
#else
    (void)path;
    return BANUX_IO_ERR_DISABLED;
#endif
}

int banux_close(const char *path)
{
#if BANUX_IO_EN
    VfsNode_t *node = banux_io_find(path);
    DrvDevice_t *device;
    int ret;

    if (!node) return BANUX_IO_ERR_NOT_FOUND;
    device = banux_io_device(node);
    if (!device) return BANUX_IO_ERR_WRONG_TYPE;
    if (!device->isOpened) return BANUX_IO_OK;
    ret = device->close ? device->close(device->privData) : 0;
    if (ret != 0) return BANUX_IO_ERR_DRIVER;
    device->isOpened = FALSE;
    return BANUX_IO_OK;
#else
    (void)path;
    return BANUX_IO_ERR_DISABLED;
#endif
}

int banux_read(const char *path, void *data, uint32_t len)
{
    return banux_read_at(path, data, len, 0u);
}

int banux_read_at(const char *path, void *data, uint32_t len, uint32_t offset)
{
#if BANUX_IO_EN
    VfsNode_t *node;
    DrvDevice_t *device;

    if (!path || !data || len == 0u || len > 0xFFFFu) {
        return BANUX_IO_ERR_INVALID;
    }
    node = banux_io_find(path);
    if (!node) return BANUX_IO_ERR_NOT_FOUND;

    if (node->type == VFS_NODE_DEV) {
        device = banux_io_device(node);
        if (!device || !device->read) return BANUX_IO_ERR_NOT_SUPPORTED;
        return device->read(device->privData, (uint8_t *)data, len);
    }
    if (node->type == VFS_NODE_PARAM) {
        return Vfs_ReadParam(node, (char *)data, (uint16_t)len);
    }
    if (node->type == VFS_NODE_FILE) {
        return Vfs_ReadFile(node, (char *)data, (uint16_t)len, offset);
    }
    return BANUX_IO_ERR_WRONG_TYPE;
#else
    (void)path; (void)data; (void)len; (void)offset;
    return BANUX_IO_ERR_DISABLED;
#endif
}

int banux_write(const char *path, const void *data, uint32_t len)
{
#if BANUX_IO_EN
    VfsNode_t *node;
    DrvDevice_t *device;
    char value[VFS_MAX_PARAM_LEN];

    if (!path || (!data && len > 0u)) return BANUX_IO_ERR_INVALID;
    node = banux_io_find(path);
    if (!node) return BANUX_IO_ERR_NOT_FOUND;

    if (node->type == VFS_NODE_DEV) {
        device = banux_io_device(node);
        if (!device || !device->write) return BANUX_IO_ERR_NOT_SUPPORTED;
        return device->write(device->privData, (const uint8_t *)data, len);
    }
    if (node->type == VFS_NODE_PARAM) {
        VfsError_t result;
        if (!data || len >= sizeof(value)) return BANUX_IO_ERR_INVALID;
        memcpy(value, data, len);
        value[len] = '\0';
        result = Vfs_WriteParam(node, value);
        if (result == VFS_OK) return (int)len;
        return result == VFS_ERR_READ_ONLY
             ? BANUX_IO_ERR_NOT_SUPPORTED : BANUX_IO_ERR_DRIVER;
    }
    return node->type == VFS_NODE_FILE
         ? BANUX_IO_ERR_NOT_SUPPORTED : BANUX_IO_ERR_WRONG_TYPE;
#else
    (void)path; (void)data; (void)len;
    return BANUX_IO_ERR_DISABLED;
#endif
}

int banux_ioctl(const char *path, uint32_t command, void *argument)
{
#if BANUX_IO_EN
    VfsNode_t *node = banux_io_find(path);
    DrvDevice_t *device;

    if (!node) return BANUX_IO_ERR_NOT_FOUND;
    device = banux_io_device(node);
    if (!device) return BANUX_IO_ERR_WRONG_TYPE;
    if (!device->ioctl) return BANUX_IO_ERR_NOT_SUPPORTED;
    return device->ioctl(device->privData, command, argument);
#else
    (void)path; (void)command; (void)argument;
    return BANUX_IO_ERR_DISABLED;
#endif
}
