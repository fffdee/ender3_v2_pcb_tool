/**
 *****************************************************************************
 * @file     drv_sdio.h
 * @brief    SDIO(SD 卡 / FatFs) 设备驱动接入驱动框架
 *****************************************************************************
 * 注册后设备节点：/driver/sdio/sd（只代表物理 SD 卡）
 *   - init  检测 SD 卡并挂载 FatFs
 *   - read  读取 block 参数指定块（整块）
 *   - write 写入 block 参数指定块（整块）
 *   - 参数：detected / type / size_mb / free_mb / mount / block(RW)
 *****************************************************************************
 */
#ifndef __DRV_SDIO_H__
#define __DRV_SDIO_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 SDIO 设备驱动（须在 DrvFramework_Init() 之后调用）
 * @return 0 成功，负数失败
 * @note   注册成功且 FatFs 已挂载时，会自动将 SD 卡文件系统挂载到 VFS /sd
 */
int DrvSdio_Register(void);

/**
 * @brief  查询 SD 卡 FatFs 是否已挂载成功
 * @return 1 已挂载，0 未挂载
 */
int DrvSdio_IsMounted(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_SDIO_H__ */
