/** @file drv_init.h @brief Portable Banux driver framework initialization. */
#ifndef __DRV_INIT_H__
#define __DRV_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize VFS, the driver filesystem and the generic device model. */
int DrvFramework_Init(void);

/** Compatibility alias; initializes framework services only, not devices. */
int DrvFramework_FullInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_INIT_H__ */
