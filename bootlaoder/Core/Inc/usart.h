/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.h
  ******************************************************************************
  */
/* USER CODE END Header */
#ifndef __USART_H__
#define __USART_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart3;

void MX_USART1_Init(void);
void MX_USART3_Init(void);

void uart_rx_start(void);
int  uart_rb_pop(UART_HandleTypeDef *huart, uint8_t *b);
void uart_tx(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USART_H__ */
