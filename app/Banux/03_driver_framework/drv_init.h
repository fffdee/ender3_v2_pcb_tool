/**
 *****************************************************************************
 * @file     drv_init.h
 * @author   BG Card Team
 * @version  V1.0.0
 * @date     02-January-2026
 * @brief    驱动框架初始化头文件
 *****************************************************************************
 */

#ifndef __DRV_INIT_H__
#define __DRV_INIT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/*******************************************************************************
 * 函数声明
 ******************************************************************************/

/**
 * @brief  初始化驱动框架核心
 * @retval 0-成功, <0-失败
 */
int DrvFramework_Init(void);

/**
 * @brief  注册所有硬件驱动
 * @retval 0-成功, <0-失败
 */
int DrvFramework_RegisterAll(void);

/**
 * @brief  注册平台设备驱动（SDIO SD 卡 / UART1、UART3）
 * @retval 0-成功, <0-失败
 * @note   须在 DrvFramework_Init() 之后调用；
 *         SDIO 设备 init 内会挂载 FatFs（f_mount）。
 */
int DrvFramework_RegisterDevices(void);

/**
 * @brief  驱动框架完整初始化(框架+驱动)
 * @retval 0-成功, <0-失败
 * 
 * @note   在main()中调用此函数完成所有驱动注册
 * 
 * @example
 *   int main(void) {
 *       // 硬件初始化...
 *       
 *       // 驱动框架初始化
 *       DrvFramework_FullInit();
 *       
 *       // Shell初始化
 *       Shell_Init();
 *       
 *       while(1) {
 *           // 主循环
 *       }
 *   }
 */
int DrvFramework_FullInit(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_INIT_H__ */
