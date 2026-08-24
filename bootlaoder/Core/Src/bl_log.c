#include "bl_log.h"
#include "usart.h"

static void uart_puts(UART_HandleTypeDef *huart, const char *s)
{
    const char *p = s;
    while (*p != '\0') {
        p++;
    }
    if (p != s) {
        (void)HAL_UART_Transmit(huart, (uint8_t *)s, (uint16_t)(p - s), 20);
    }
}

void bl_log(const char *s)
{
    uart_puts(&huart1, s);
    uart_puts(&huart1, "\r\n");
    uart_puts(&huart3, s);
    uart_puts(&huart3, "\r\n");
}

void bl_log_u32(const char *prefix, uint32_t value)
{
    char buf[48];
    const char hex[] = "0123456789ABCDEF";
    int i = 0;
    int n = 0;

    if (prefix != NULL) {
        while (prefix[n] != '\0' && n < 28) {
            buf[n] = prefix[n];
            n++;
        }
    }
    buf[n++] = '0';
    buf[n++] = 'x';
    for (i = 7; i >= 0; i--) {
        buf[n++] = hex[(value >> (i * 4)) & 0xFu];
    }
    buf[n] = '\0';
    bl_log(buf);
}
