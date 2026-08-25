/**
 *****************************************************************************
 * @file    shell_io_uart.c
 * @brief   Shell 的 UART IO 适配层 (APP 移植新增)
 *
 * 说明:
 *   - ShellIO_Uart1_Get(): UART1 @ 2M baud (上位机 / 调试终端)
 *   - ShellIO_Uart3_Get(): UART3 @ 115200 (调试串口)
 *   - 接收走 usart.c 的中断环形缓冲, 发送为阻塞 TX
 *****************************************************************************
 */
#include "shell_io_uart.h"
#include "usart.h"
#include "app_bl.h"

static uint16_t uart1_send(uint8_t *data, uint16_t len)
{
    uart_tx(&huart1, data, len);
    return len;
}

/* UART1 数据由 app_bl_poll() 从环形缓冲消费（嗅探 ENTER_BOOT 帧）并镜像转发，
 * 此处改从镜像缓冲读取，避免与嗅探抢数据。 */
static uint16_t uart1_recv(uint8_t *data, uint16_t maxLen)
{
    return app_bl_shell1_pop(data, maxLen);
}

static uint16_t uart1_available(void)
{
    return app_bl_shell1_available();
}

static const ShellIO_t s_uart1_io = {
    "UART1",
    uart1_send,
    uart1_recv,
    uart1_available
};

static uint16_t uart3_send(uint8_t *data, uint16_t len)
{
    uart_tx(&huart3, data, len);
    return len;
}

/* UART3 数据同样由 app_bl_poll() 从环形缓冲消费（嗅探 ENTER_BOOT 帧）并镜像转发，
 * 此处改从 shell3 镜像缓冲读取，与 UART1 保持一致，避免与嗅探抢数据。 */
static uint16_t uart3_recv(uint8_t *data, uint16_t maxLen)
{
    return app_bl_shell3_pop(data, maxLen);
}

static uint16_t uart3_available(void)
{
    return app_bl_shell3_available();
}

static const ShellIO_t s_uart3_io = {
    "UART3",
    uart3_send,
    uart3_recv,
    uart3_available
};

/* UART1+UART3 组合控制台 IO：
 * - send:     双发（UART1 @2M + UART3 @115200），两个端口都能看到输出
 * - recv:     从 shell1 优先读，不足再从 shell3 镜像缓冲读
 * - available: shell1 + shell3 可读字节数之和
 * 注意：UART3 设备节点（drv_uart.c）同样从 shell3 镜像读，但当前无命令调用其
 *       read 接口，无实际抢占冲突。
 */
static uint16_t uart_all_send(uint8_t *data, uint16_t len)
{
    if (data && len > 0) {
        uart_tx(&huart1, data, len);
        uart_tx(&huart3, data, len);
    }
    return len;
}

static uint16_t uart_all_recv(uint8_t *data, uint16_t maxLen)
{
    uint16_t n = app_bl_shell1_pop(data, maxLen);
    if (n < maxLen) {
        n = (uint16_t)(n + app_bl_shell3_pop(data + n, (uint16_t)(maxLen - n)));
    }
    return n;
}

static uint16_t uart_all_available(void)
{
    return (uint16_t)(app_bl_shell1_available() + app_bl_shell3_available());
}

static const ShellIO_t s_uart_all_io = {
    "UART1+UART3",
    uart_all_send,
    uart_all_recv,
    uart_all_available
};

const ShellIO_t *ShellIO_Uart1_Get(void)
{
    return &s_uart1_io;
}

const ShellIO_t *ShellIO_Uart3_Get(void)
{
    return &s_uart3_io;
}

const ShellIO_t *ShellIO_UartAll_Get(void)
{
    return &s_uart_all_io;
}

void ShellIO_Uart_Flush(void)
{
    uint8_t b;

    while (uart_rb_available(&huart1) > 0) {
        (void)uart_rb_pop(&huart1, &b);
    }
    while (uart_rb_available(&huart3) > 0) {
        (void)uart_rb_pop(&huart3, &b);
    }
}

