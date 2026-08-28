/**
 *****************************************************************************
 * @file     drv_uart.c
 * @brief    UART 设备驱动接入驱动框架
 *
 * 注册两个设备节点：
 *   - /driver/uart/uart1  USART1 @2M（Shell/升级口）
 *   - /driver/uart/uart3  USART3 @115200（Shell/WiFi）
 *
 * RX 数据流：UART 中断 → 环形缓冲 → app_bl_poll()（嗅探 ENTER_BOOT 帧）
 *           → 镜像缓冲 → Shell / 本设备 read。
 * write 走阻塞发送（uart_tx）。
 *****************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "drv_uart.h"
#include "drv_fs.h"
#include "drv_device.h"
#include "debug.h"
#include "usart.h"
#include "app_bl.h"   /* app_bl_shell1/shell3 镜像缓冲 */

/*===========================================================================
 * 私有数据
 *===========================================================================*/
typedef struct {
    UART_HandleTypeDef *huart;                        /* 发送句柄 */
    uint32_t            baud;                         /* 波特率 */
    uint16_t (*rxAvail)(void);                        /* 镜像缓冲可读字节数 */
    uint16_t (*rxPop)(uint8_t *data, uint16_t maxLen);/* 从镜像缓冲取数据 */
} UartPriv_t;

static UartPriv_t s_uart1 = {
    &huart1, 2000000u,
    app_bl_shell1_available, app_bl_shell1_pop,
};
static UartPriv_t s_uart3 = {
    &huart3, 115200u,
    app_bl_shell3_available, app_bl_shell3_pop,
};

/*===========================================================================
 * 驱动操作接口
 *===========================================================================*/
static int uart_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    UartPriv_t *p = (UartPriv_t *)priv;
    if (!buf || !len) {
        return 0;
    }
    return (int)p->rxPop(buf, (uint16_t)len);
}

static int uart_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    UartPriv_t *p = (UartPriv_t *)priv;
    if (!buf || !len) {
        return 0;
    }
    uart_tx(p->huart, buf, (uint16_t)len);
    return (int)len;
}

/*===========================================================================
 * 参数回调
 *===========================================================================*/
static int get_baud(char *buf, uint16_t maxLen, void *userData)
{
    UartPriv_t *p = (UartPriv_t *)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)p->baud);
}

static int get_rx_avail(char *buf, uint16_t maxLen, void *userData)
{
    UartPriv_t *p = (UartPriv_t *)userData;
    return snprintf(buf, maxLen, "%u", (unsigned)p->rxAvail());
}

static const FsParamDef_t uart_params[] = {
    FS_PARAM_DEF("baud",     "baud rate (Hz)",              get_baud,    NULL),
    FS_PARAM_DEF("rx_avail", "bytes available in RX mirror", get_rx_avail, NULL),
    FS_PARAM_END
};

/*===========================================================================
 * 设备注册
 *===========================================================================*/
/* 注意：不能用const，因为 DrvDevice_Register 运行时需修改 fsNode/isRegistered 字段 */
static DrvDevice_t uart1_drv = {
    .name     = "uart1",
    .desc     = "USART1 @2M (shell / upgrade)",
    .bus      = DRV_BUS_UART,
    .init     = NULL,
    .deinit   = NULL,
    .open     = NULL,
    .close    = NULL,
    .read     = uart_drv_read,
    .write    = uart_drv_write,
    .ioctl    = NULL,
    .params   = uart_params,
    .privData = &s_uart1,
};

static DrvDevice_t uart3_drv = {
    .name     = "uart3",
    .desc     = "USART3 @115200 (shell)",
    .bus      = DRV_BUS_UART,
    .init     = NULL,
    .deinit   = NULL,
    .open     = NULL,
    .close    = NULL,
    .read     = uart_drv_read,
    .write    = uart_drv_write,
    .ioctl    = NULL,
    .params   = uart_params,
    .privData = &s_uart3,
};

int DrvUart_Register(void)
{
    int r1 = DrvDevice_Register((DrvDevice_t *)&uart1_drv);
    int r2 = DrvDevice_Register((DrvDevice_t *)&uart3_drv);
    if (r1 != 0 || r2 != 0) {
        DBG("[DrvUart] register failed: r1=%d r2=%d\n", r1, r2);
        return -1;
    }
    return 0;
}
