/**
 *****************************************************************************
 * @file     drv_uart.h
 * @brief    UART 设备驱动接入驱动框架
 *****************************************************************************
 * 注册后设备节点：
 *   - /driver/uart/uart1  (USART1 @ 2M，Shell/升级口)
 *   - /driver/uart/uart3  (USART3 @ 115200，Shell/WiFi)
 * RX 数据经 app_bl_poll() 嗅探后镜像转发，设备 read 从镜像缓冲取数。
 *****************************************************************************
 */
#ifndef __DRV_UART_H__
#define __DRV_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  注册 UART 设备驱动（须在 DrvFramework_Init() 之后调用）
 * @return 0 成功，负数失败
 */
int DrvUart_Register(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_UART_H__ */
