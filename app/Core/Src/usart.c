/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   UART1 / UART3 异步收发。UART1 = 2M（Bootloader / 直连调试链路）；
  *          UART3 = 115200（ESP 桥接）。
  *          RX 采用 DMA 循环模式 + 软件环形缓冲：硬件自动搬运字节，根除每字节中断
  *          在 2M 高吞吐(如下载 gcode)下因延迟导致的硬件溢出(ORE)与乱码；
  *          阻塞发送用于调试打印。
  ******************************************************************************
  */
/* USER CODE END Header */
#include "usart.h"

/* USER CODE BEGIN 0 */
#include <string.h>

/*******************************************************************************
 * RX：DMA 循环模式 + 软件环形缓冲
 * ---------------------------------------------------------------------------
 * 原方案为中断每字节接收(HAL_UART_Receive_IT)：2M 波特率下每字节触发一次 USART
 * 中断，gcode 大文件下载时若被其他中断/主循环延迟，硬件 RX 溢出(ORE)丢字节 →
 * 接收端出现乱码。
 *
 * 现改为：UART RX 走 DMA 循环模式，硬件自动把字节搬进 dma[] 缓冲；软件通过读
 * DMA 剩余计数器(CNDTR)计算"已写入位置"，把新增字节搬进更大的软件环形缓冲 rb[]；
 * 应用层 uart_rb_pop / uart_rb_available 在取数前先 drain。这样彻底消除每字节
 * 中断，仅在 DMA 半满/全满(每 UART_DMA_BUF/2 字节)时进一次轻量中断兜底。
 * 注：TX 仍为阻塞发送(HAL_UART_Transmit)，下载场景 TX 量很小，无丢字节风险。
 ******************************************************************************/
#define UART_RB_SIZE    4096u   /* 软件环形缓冲(应用层消费) */
#define UART_DMA_BUF    512u    /* DMA 循环缓冲(硬件搬运)   */

typedef struct {
    uint8_t  rb[UART_RB_SIZE];           /* 软件环形缓冲 */
    volatile uint16_t rb_head;
    volatile uint16_t rb_tail;
    uint8_t  dma[UART_DMA_BUF];          /* DMA 循环目标缓冲 */
    volatile uint16_t dma_last;          /* 上次已搬入 rb 的 DMA 写入位置 */
} uart_drv_t;

static uart_drv_t s_uart1;
static uart_drv_t s_uart3;

/* DMA 句柄(供 stm32f1xx_it.c 的 DMA 通道 ISR 引用) */
DMA_HandleTypeDef hdma_usart1_rx;
DMA_HandleTypeDef hdma_usart3_rx;

/* ── 软件环形缓冲(生产者:DMA 兜底/主循环 drain; 消费者:app_bl_poll) ── */
static void rb_push(uart_drv_t *u, uint8_t b)
{
    uint16_t next = (uint16_t)((u->rb_head + 1u) % UART_RB_SIZE);
    if (next != u->rb_tail) {            /* 满则丢弃最旧，避免覆盖未读数据 */
        u->rb[u->rb_head] = b;
        u->rb_head = next;
    }
}

/* 把 DMA 循环缓冲里"自上次 drain 以来新增"的字节搬进软件环形缓冲。
 * 注意：本函数会被主循环(uart_rb_pop/available)与 DMA 半满/全满中断并发调用，
 * 因此整个"读位置→拷贝→更新 dma_last"必须在临界区内完成，否则两处交错会导致
 * 环形缓冲重复拷贝、数据错位。拷贝量 ≤ UART_DMA_BUF 字节，关中断仅数微秒，
 * DMA 硬件在此期间照常接收，不会丢字节。 */
static void uart_rb_drain(uart_drv_t *u, DMA_HandleTypeDef *hdma)
{
    uint16_t pos, cnt, i;

    __disable_irq();
    pos = (uint16_t)(UART_DMA_BUF - __HAL_DMA_GET_COUNTER(hdma));
    cnt = (uint16_t)((pos - u->dma_last) % UART_DMA_BUF);
    for (i = 0; i < cnt; i++) {
        uint16_t idx = (uint16_t)((u->dma_last + i) % UART_DMA_BUF);
        rb_push(u, u->dma[idx]);
    }
    u->dma_last = pos;
    __enable_irq();
}

static uart_drv_t *uart_drv_of(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) return &s_uart1;
    if (huart == &huart3) return &s_uart3;
    return (uart_drv_t *)0;
}

static DMA_HandleTypeDef *uart_hdma_of(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) return &hdma_usart1_rx;
    if (huart == &huart3) return &hdma_usart3_rx;
    return (DMA_HandleTypeDef *)0;
}

void uart_rx_start(void)
{
    /* 清空全部状态(含 dma_last / rb 指针)。DMA 尚未启动，此时清零安全。 */
    memset(&s_uart1, 0, sizeof(s_uart1));
    memset(&s_uart3, 0, sizeof(s_uart3));
    /* 启动 DMA 循环接收；依赖 MSP 中已 LINK 的 hdmarx */
    (void)HAL_UART_Receive_DMA(&huart1, s_uart1.dma, UART_DMA_BUF);
    (void)HAL_UART_Receive_DMA(&huart3, s_uart3.dma, UART_DMA_BUF);
}

int uart_rb_pop(UART_HandleTypeDef *huart, uint8_t *b)
{
    uart_drv_t *u = uart_drv_of(huart);
    if (u == (uart_drv_t *)0 || b == (uint8_t *)0) {
        return 0;
    }
    uart_rb_drain(u, uart_hdma_of(huart));
    if (u->rb_head == u->rb_tail) {
        return 0;
    }
    *b = u->rb[u->rb_tail];
    u->rb_tail = (uint16_t)((u->rb_tail + 1u) % UART_RB_SIZE);
    return 1;
}

uint16_t uart_rb_available(UART_HandleTypeDef *huart)
{
    uart_drv_t *u = uart_drv_of(huart);
    uint16_t head, tail;
    if (u == (uart_drv_t *)0) {
        return 0;
    }
    uart_rb_drain(u, uart_hdma_of(huart));
    head = u->rb_head;
    tail = u->rb_tail;
    return (uint16_t)((head + UART_RB_SIZE - tail) % UART_RB_SIZE);
}

void uart_tx(UART_HandleTypeDef *huart, const uint8_t *data, uint16_t len)
{
    if (data != (const uint8_t *)0 && len > 0u) {
        (void)HAL_UART_Transmit(huart, (uint8_t *)data, len, 1000);
    }
}

/* DMA 半满/全满回调：进一次轻量中断把硬件缓冲搬进软件环形缓冲(兜底，
 * 确保主循环偶发阻塞时也不会因 DMA 循环覆盖未读数据而丢字节)。 */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *huart)
{
    uart_drv_t *u = uart_drv_of(huart);
    if (u != (uart_drv_t *)0) {
        uart_rb_drain(u, uart_hdma_of(huart));
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uart_drv_t *u = uart_drv_of(huart);
    if (u != (uart_drv_t *)0) {
        uart_rb_drain(u, uart_hdma_of(huart));
    }
}

/* DMA 接收出错(如 ORE)时 HAL 会中止 DMA，必须重启循环接收；
 * 重启后 CNDTR 复位为满，故把 dma_last 归零以免把旧数据误判为新增。 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    uart_drv_t *u = uart_drv_of(huart);
    DMA_HandleTypeDef *hdma = uart_hdma_of(huart);

    /* 清 ORE/FE/NE：F1 要求先读 SR 再读 DR */
    volatile uint32_t tmp = huart->Instance->SR;
    (void)tmp;
    tmp = huart->Instance->DR;
    (void)tmp;
    __HAL_UART_CLEAR_FLAG(huart, UART_FLAG_PE);

    if ((u != (uart_drv_t *)0) && (hdma != (DMA_HandleTypeDef *)0)) {
        (void)HAL_UART_Receive_DMA(huart, u->dma, UART_DMA_BUF);
        u->dma_last = 0;
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
  /* ESP 桥接串口：维持 2000000(ESP 链路固定速率)。RX 现已走 DMA 循环模式，
   * 即便提到 2M 也不会再因每字节中断延迟而产生硬件溢出(ORE)丢字节。 */
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

    /* USART1 RX → DMA1 通道5，循环模式(硬件自动循环搬运，无每字节中断) */
    __HAL_RCC_DMA1_CLK_ENABLE();
    hdma_usart1_rx.Instance = DMA1_Channel5;
    hdma_usart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart1_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_usart1_rx) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(uartHandle, hdmarx, hdma_usart1_rx);

    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);
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

    /* USART3 RX → DMA1 通道3，循环模式 */
    __HAL_RCC_DMA1_CLK_ENABLE();
    hdma_usart3_rx.Instance = DMA1_Channel3;
    hdma_usart3_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart3_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart3_rx.Init.Priority = DMA_PRIORITY_HIGH;
    if (HAL_DMA_Init(&hdma_usart3_rx) != HAL_OK)
    {
      Error_Handler();
    }
    __HAL_LINKDMA(uartHandle, hdmarx, hdma_usart3_rx);

    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);
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

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
