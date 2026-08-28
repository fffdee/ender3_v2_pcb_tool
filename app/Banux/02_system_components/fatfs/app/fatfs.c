/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
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
#include "fatfs.h"
#include "banux_config.h"
#include "banux_component.h"

BANUX_COMPONENT_DEFINE(g_banux_component_fatfs,
                       "fatfs", "R0.12c", BANUX_COMPONENT_SYSTEM,
                       BANUX_FATFS_EN,
                       "FAT filesystem support");

uint8_t retSD = 1u;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */
uint8_t retFlash = 1u;
char FlashPath[4];
FATFS FlashFatFS;

/* USER CODE BEGIN Variables */

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
#if BANUX_FATFS_EN
  retSD = FATFS_LinkDriver((Diskio_drvTypeDef *)&SD_Driver, SDPath);
#if BANUX_INTERNAL_FLASH_FS_EN
  retFlash = FATFS_LinkDriver((Diskio_drvTypeDef *)&InternalFlash_Driver,
                              FlashPath);
#else
  retFlash = 1u;
#endif
  BanuxComponent_SetState("fatfs", retSD == 0u &&
                          (!BANUX_INTERNAL_FLASH_FS_EN || retFlash == 0u)
                          ? BANUX_COMPONENT_READY
                          : BANUX_COMPONENT_FAILED);
#else
  retSD = 1u;
  retFlash = 1u;
#endif

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  return 0;
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
