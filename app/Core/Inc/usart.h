/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  * @brief   This file contains all the function prototypes for
  *          the usart.c file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern UART_HandleTypeDef huart1;

extern UART_HandleTypeDef huart3;

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

void MX_USART1_Init(void);
void MX_USART3_Init(void);

/* USER CODE BEGIN Prototypes */

/* UART 传输/接收辅助接口（与 Bootloader 保持一致） */
void uart_rx_start(void);                          /* 启动两路 UART 中断接收 */
int  uart_rb_pop(UART_HandleTypeDef *huart, uint8_t *b); /* 从环形缓冲取一个字节 */
uint16_t uart_rb_available(UART_HandleTypeDef *huart);  /* 环形缓冲中可读字节数 */
void uart_tx(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len);

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */

