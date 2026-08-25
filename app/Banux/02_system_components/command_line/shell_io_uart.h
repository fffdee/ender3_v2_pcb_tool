/**
 *****************************************************************************
 * @file    shell_io_uart.h
 * @brief   Shell 的 UART IO 适配层 (APP 移植新增)
 *
 * 将 bg_shell 的 ShellIO_t 抽象接口对接 STM32 HAL 串口,
 * 替代原工程依赖 FreeRTOS / CDC / BLE 的 shell_io_manager。
 *****************************************************************************
 */
#ifndef __SHELL_IO_UART_H__
#define __SHELL_IO_UART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "bg_shell.h"

/* 获取 UART1 的 Shell IO (2M baud, 上位机/调试终端) */
const ShellIO_t *ShellIO_Uart1_Get(void);

/* 获取 UART3 的 Shell IO (115200, 调试串口) */
const ShellIO_t *ShellIO_Uart3_Get(void);

/* 获取 UART1+UART3 组合 Shell IO (输出双发 / 输入双收, 任一端口可用) */
const ShellIO_t *ShellIO_UartAll_Get(void);

/* 清空两路 UART 环形缓冲, 切换 IO 前调用避免残留数据 */
void ShellIO_Uart_Flush(void);

#ifdef __cplusplus
}
#endif

#endif /* __SHELL_IO_UART_H__ */
