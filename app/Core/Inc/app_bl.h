/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_bl.h
  * @brief   APP 侧 Bootloader 联动：UART 嗅探 ENTER_BOOT 协议帧，
  *          置位配置区 enter_boot 标志后复位，让 Bootloader 停留在升级模式。
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __APP_BL_H__
#define __APP_BL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void app_bl_init(void);      /* 初始化嗅探状态机，启动 UART 接收 */
void app_bl_poll(void);      /* 主循环轮询：处理 UART 字节流 */
int  app_bl_enter_boot(void); /* 置位 enter_boot 魔数后软复位，成功返回 0（不返回），失败返回 -1 */
uint16_t app_bl_shell1_available(void); /* Shell 镜像缓冲（UART1）可读字节数 */
uint16_t app_bl_shell1_pop(uint8_t *data, uint16_t maxLen); /* 从 Shell 镜像缓冲（UART1）取数据 */
uint16_t app_bl_shell3_available(void); /* UART3 镜像缓冲可读字节数（/driver/uart/uart3 read 用） */
uint16_t app_bl_shell3_pop(uint8_t *data, uint16_t maxLen); /* 从 UART3 镜像缓冲取数据 */
void app_log(const char *s); /* 调试打印（双 UART 输出） */
void app_log_u32(const char *prefix, uint32_t value); /* 打印 prefix+0xXXXXXXXX */

#ifdef __cplusplus
}
#endif

#endif /* __APP_BL_H__ */
