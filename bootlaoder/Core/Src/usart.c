/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   UART1 / UART3 for bootloader protocol and debug log.
  ******************************************************************************
  */
/* USER CODE END Header */
#include "usart.h"
#include "bl_config.h"

/* USER CODE BEGIN 0 */
#define UART_RB_SIZE 1024u

typedef struct {
    uint8_t  buf[UART_RB_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
} uart_rb_t;

static uart_rb_t s_rb1;
static uart_rb_t s_rb3;
static uint8_t s_rx1;
static uint8_t s_rx3;

static void rb_push(uart_rb_t *rb, uint8_t b)
{
    uint16_t next = (uint16_t)((rb->head + 1u) % UART_RB_SIZE);
    if (next != rb->tail) {
        rb->buf[rb->head] = b;
        rb->head = next;
    }
}

static int rb_pop(uart_rb_t *rb, uint8_t *b)
{
    if (rb->head == rb->tail) {
        return 0;
    }
    *b = rb->buf[rb->tail];
    rb->tail = (uint16_t)((rb->tail + 1u) % UART_RB_SIZE);
    return 1;
}

void uart_rx_start(void)
{
    s_rb1.head = 0;
    s_rb1.tail = 0;
    s_rb3.head = 0;
    s_rb3.tail = 0;
    (void)HAL_UART_Receive_IT(&huart1, &s_rx1, 1);
    (void)HAL_UART_Receive_IT(&huart3, &s_rx3, 1);
}

int uart_rb_pop(UART_HandleTypeDef *huart, uint8_t *b)
{
    if (huart == &huart1) {
        return rb_pop(&s_rb1, b);
    }
    if (huart == &huart3) {
        return rb_pop(&s_rb3, b);
    }
    return 0;
}

void uart_tx(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len)
{
    if (data != NULL && len > 0u) {
        (void)HAL_UART_Transmit(huart, (uint8_t *)data, len, 1000);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        rb_push(&s_rb1, s_rx1);
        (void)HAL_UART_Receive_IT(&huart1, &s_rx1, 1);
    } else if (huart == &huart3) {
        rb_push(&s_rb3, s_rx3);
        (void)HAL_UART_Receive_IT(&huart3, &s_rx3, 1);
    }
}
/* USER CODE END 0 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart3;

void MX_USART1_Init(void)
{
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 2000000;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
}

void MX_USART3_Init(void)
{
  huart3.Instance = USART3;
  /* 2000000 @APB1=32MHz：BRR=32M/(16*115200)=17.36，取 17 → 实际 117647，误差 +2.12%（边际，可工作） */
  huart3.Init.BaudRate = 2000000;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(uartHandle->Instance==USART1)
  {
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**USART1: PA9 TX, PA10 RX */
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
  }
  else if(uartHandle->Instance==USART3)
  {
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**USART3: PB10 TX, PB11 RX */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{
  if(uartHandle->Instance==USART1)
  {
    __HAL_RCC_USART1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_9|GPIO_PIN_10);
    HAL_NVIC_DisableIRQ(USART1_IRQn);
  }
  else if(uartHandle->Instance==USART3)
  {
    __HAL_RCC_USART3_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_10|GPIO_PIN_11);
    HAL_NVIC_DisableIRQ(USART3_IRQn);
  }
}
