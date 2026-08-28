/**
 * @file banux_io.h
 * @brief Path-based application access to VFS driver nodes.
 */
#ifndef BANUX_IO_H
#define BANUX_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BANUX_IO_OK                = 0,
    BANUX_IO_ERR_INVALID       = -1,
    BANUX_IO_ERR_NOT_FOUND     = -2,
    BANUX_IO_ERR_WRONG_TYPE    = -3,
    BANUX_IO_ERR_NOT_SUPPORTED = -4,
    BANUX_IO_ERR_DISABLED      = -5,
    BANUX_IO_ERR_DRIVER        = -6
} BanuxIoError_t;

void BanuxIo_Init(void);

/* Device paths use the driver's binary callbacks. Parameter paths use UTF-8
 * text values. File paths support read through the mounted VFS backend. */
int banux_open(const char *path);
int banux_close(const char *path);
int banux_read(const char *path, void *data, uint32_t len);
int banux_read_at(const char *path, void *data, uint32_t len, uint32_t offset);
int banux_write(const char *path, const void *data, uint32_t len);
int banux_ioctl(const char *path, uint32_t command, void *argument);

#ifdef __cplusplus
}
#endif

#endif /* BANUX_IO_H */
