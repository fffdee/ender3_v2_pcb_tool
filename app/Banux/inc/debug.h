/**
 ******************************************************************************
 * @file    debug.h
 * @brief   Banux 框架调试输出支撑头文件
 *
 * 作用:
 *   - 将框架的 DBG() 宏统一映射到 APP 串口日志通道
 *   - 覆盖 banux_config.h 中默认的 DBG=printf (printf 无重定向时在 MCU 上会死锁)
 *   - 本头文件无条件覆盖 DBG, 与 include 顺序无关
 ******************************************************************************
 */
#ifndef __DEBUG_H__
#define __DEBUG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* APP 日志输出 (UART1+UART3 双通道), 实现在 Core/Src/app_bl.c */
void app_log(const char *s);

/* 带格式化参数的日志输出, 实现在 04_shell_commands/shell_io_uart.c */
void app_log_printf(const char *fmt, ...);

/* 统一映射 DBG -> app_log_printf */
#ifdef DBG
#undef DBG
#endif
#define DBG(fmt, ...)   app_log_printf(fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* __DEBUG_H__ */
