/**
 * @file drv_sdcard.h
 * @brief SD卡驱动框架适配层
 */

#ifndef __DRV_SDCARD_H__
#define __DRV_SDCARD_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册SD卡驱动到驱动框架
 * @return 0=成功，-1=失败
 */
int SDCard_DrvRegister(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_SDCARD_H__ */
