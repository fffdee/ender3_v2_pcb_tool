/** @file drv_timer1ms.h @brief Hardware-backed 1 ms system timer driver. */
#ifndef __DRV_TIMER1MS_H__
#define __DRV_TIMER1MS_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t DrvTimer1ms_Now(void);
int DrvTimer1ms_Expired(uint32_t deadline);
int DrvTimer1ms_Register(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_TIMER1MS_H__ */
