/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "fatfs.h"
#include "sdio.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_bl.h"
#include "drv_init.h"       /* Banux: 驱动框架初始化 */
#include "bg_shell.h"       /* Banux: Shell 核心 */
#include "shell_io_uart.h"  /* Banux: Shell UART IO */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* APP 链接在 0x08008000（Bootloader 之后），必须把向量表偏移到应用区，
   * 否则中断向量仍指向 Bootloader 的 0x08000000，任何中断都会跑飞。 */
#define APP_VECTOR_TABLE_ADDR   0x08008000u
  SCB->VTOR = APP_VECTOR_TABLE_ADDR;

  /* 恢复全局中断：BL 跳转前调用 __disable_irq()（PRIMASK=1），软件跳转不清 PRIMASK，
   * 若不恢复，所有中断（含 USART1/3 RX）保持屏蔽，UART 收不到任何数据。 */
  __enable_irq();

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SDIO_SD_Init();
  MX_USART3_Init();
  MX_FATFS_Init();
  MX_USART1_Init();
  /* USER CODE BEGIN 2 */

  /* 启动日志：与 Bootloader 同一条 UART 链路输出 */
  app_log("\r\n");
  app_log("========== APP ==========\r\n");
  app_log_u32("clk: SYSCLK=", HAL_RCC_GetSysClockFreq());
  app_log("UPGRADE: listening ENTER_BOOT (UART1 @2000000 / UART3 @115200) [diag]\r\n");

  /* 启动 UART 中断接收，嗅探上位机的 ENTER_BOOT 帧 */
  app_bl_init();

  /* ─── Banux Shell 接入 ───
   * 顺序不能乱：
   *   1. DrvFramework_Init()        VFS 驱动文件系统
   *   1.5 DrvFramework_RegisterDevices() 注册平台设备驱动（SDIO SD 卡挂载
   *        FatFs + /driver/uart/uart1、uart3 节点），须在框架 Init 之后
   *   2. Shell_Init()               先初始化 Shell（内部注册 help 模块）
   *   3. Shell_SetIO()              挂载 IO（UART1 @2M，经 app_bl 镜像转发）
   *   4. DrvFramework_RegisterAll() 注册命令模块（必须在 Shell_Init 之后，
   *                                  否则模块注册表会被 Shell_Init 清空）
   */
  DrvFramework_Init();
  DrvFramework_RegisterDevices();
  Shell_Init();
  if (!Shell_SetIO(ShellIO_UartAll_Get())) {
      app_log("UPGRADE: Shell_SetIO FAILED\r\n");
  } else {
      app_log("UPGRADE: Shell IO = ");
      app_log(Shell_GetIOName());
      app_log("\r\n");
  }
  DrvFramework_RegisterAll();
  Shell_Print("\r\n[APP] Banux Shell ready: help -a / ls / drivers / boot\r\n");
  app_log("UPGRADE: entering main loop\r\n");

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    app_bl_poll();   /* UART 嗅探（ENTER_BOOT 帧/"boot"），同时转发字节到 Shell 镜像缓冲 */
    Shell_Process(); /* 从镜像缓冲取数据，处理 Shell 命令 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** 与 Bootloader 一致的 64MHz 系统时钟（HSE 8MHz x PLL x8），
   *  USART1=APB2/1=64MHz、USART3=APB1/2=32MHz，保证 2M baud 精确。
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL8;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
