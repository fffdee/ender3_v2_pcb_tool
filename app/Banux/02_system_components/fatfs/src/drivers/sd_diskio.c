/**
  ******************************************************************************
  * @file    sd_diskio.c
  * @author  MCD Application Team
  * @version V1.4.1
  * @date    14-February-2017
  * @brief   SD Disk I/O driver
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT 2017 STMicroelectronics</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without 
  * modification, are permitted, provided that the following conditions are met:
  *
  * 1. Redistribution of source code must retain the above copyright notice, 
  *    this list of conditions and the following disclaimer.
  * 2. Redistributions in binary form must reproduce the above copyright notice,
  *    this list of conditions and the following disclaimer in the documentation
  *    and/or other materials provided with the distribution.
  * 3. Neither the name of STMicroelectronics nor the names of other 
  *    contributors to this software may be used to endorse or promote products 
  *    derived from this software without specific written permission.
  * 4. This software, including modifications and/or derivative works of this 
  *    software, must execute solely and exclusively on microcontroller or
  *    microprocessor devices manufactured by or for STMicroelectronics.
  * 5. Redistribution and use of this software other than as permitted under 
  *    this license is void and will automatically terminate your rights under 
  *    this license. 
  *
  * THIS SOFTWARE IS PROVIDED BY STMICROELECTRONICS AND CONTRIBUTORS "AS IS" 
  * AND ANY EXPRESS, IMPLIED OR STATUTORY WARRANTIES, INCLUDING, BUT NOT 
  * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A 
  * PARTICULAR PURPOSE AND NON-INFRINGEMENT OF THIRD PARTY INTELLECTUAL PROPERTY
  * RIGHTS ARE DISCLAIMED TO THE FULLEST EXTENT PERMITTED BY LAW. IN NO EVENT 
  * SHALL STMICROELECTRONICS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, 
  * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF 
  * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
  * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */ 

/* Includes ------------------------------------------------------------------*/
#include <string.h>
#include "ff_gen_drv.h"
#include "debug.h"
#include "sdio.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
#define SD_READY_TIMEOUT_MS  2000U

/* Private variables ---------------------------------------------------------*/
/* Disk status */
static volatile DSTATUS Stat = STA_NOINIT;
#if defined(__CC_ARM)
__align(4) static BYTE s_sectorBuffer[512];
#elif defined(__GNUC__)
static BYTE s_sectorBuffer[512] __attribute__((aligned(4)));
#else
static BYTE s_sectorBuffer[512];
#endif

/* Private function prototypes -----------------------------------------------*/
DSTATUS SD_initialize (BYTE);
DSTATUS SD_status (BYTE);
DRESULT SD_read (BYTE, BYTE*, DWORD, UINT);
#if _USE_WRITE == 1
  DRESULT SD_write (BYTE, const BYTE*, DWORD, UINT);
#endif /* _USE_WRITE == 1 */
#if _USE_IOCTL == 1
  DRESULT SD_ioctl (BYTE, BYTE, void*);
#endif  /* _USE_IOCTL == 1 */

static DRESULT SD_WaitCardReady(void);

/* 错误打印限流：SD 写/读失败时若逐扇区 DBG，传输中会以几百行/秒的速度
 * 淹没 USART，导致上位机被冲垮/掉线（表现为"无法探测在线"）。这里只在首次
 * 失败和之后每 50 次时打印，并在某个扇区成功时清零计数，使偶发错误仍能暴露。 */
static uint32_t s_sdWriteErrs = 0U;
static uint32_t s_sdReadErrs  = 0U;

static void sd_report_write_error(DWORD sector)
{
  if (s_sdWriteErrs == 0U || (s_sdWriteErrs % 50U) == 0U) {
    DBG("[SD diskio] write failed: sector=%lu error=0x%08lX "
        "state=%lu STA=0x%08lX DCTRL=0x%08lX edge=%s (%lu total)\n",
        (unsigned long)sector, (unsigned long)hsd.ErrorCode,
        (unsigned long)hsd.State, (unsigned long)SDIO->STA,
        (unsigned long)SDIO->DCTRL,
        hsd.Init.ClockEdge == SDIO_CLOCK_EDGE_FALLING ? "falling" : "rising",
        (unsigned long)s_sdWriteErrs);
  }
  s_sdWriteErrs++;
}

static void sd_report_read_error(DWORD sector)
{
  if (s_sdReadErrs == 0U || (s_sdReadErrs % 50U) == 0U) {
    DBG("[SD diskio] read failed: sector=%lu error=0x%08lX "
        "state=%lu STA=0x%08lX (%lu total)\n",
        (unsigned long)sector, (unsigned long)hsd.ErrorCode,
        (unsigned long)hsd.State, (unsigned long)SDIO->STA,
        (unsigned long)s_sdReadErrs);
  }
  s_sdReadErrs++;
}

const Diskio_drvTypeDef  SD_Driver =
{
  SD_initialize,
  SD_status,
  SD_read, 
#if  _USE_WRITE == 1
  SD_write,
#endif /* _USE_WRITE == 1 */
  
#if  _USE_IOCTL == 1
  SD_ioctl,
#endif /* _USE_IOCTL == 1 */
};

/* Private functions ---------------------------------------------------------*/

static DRESULT SD_WaitCardReady(void)
{
  uint32_t tickstart = HAL_GetTick();

  do
  {
    if(BSP_SD_GetCardState() == MSD_OK)
    {
      return RES_OK;
    }
    HAL_Delay(1U);
  } while((HAL_GetTick() - tickstart) < SD_READY_TIMEOUT_MS);

  return RES_ERROR;
}

/**
  * @brief  Initializes a Drive
  * @param  lun : not used 
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_initialize(BYTE lun)
{
  Stat = STA_NOINIT;
  
  /* Configure the uSD device */
  if(BSP_SD_Init() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }

  return Stat;
}

/**
  * @brief  Gets Disk Status
  * @param  lun : not used
  * @retval DSTATUS: Operation status
  */
DSTATUS SD_status(BYTE lun)
{
  Stat = STA_NOINIT;

  if(BSP_SD_GetCardState() == MSD_OK)
  {
    Stat &= ~STA_NOINIT;
  }
  
  return Stat;
}

/**
  * @brief  Reads Sector(s)
  * @param  lun : not used
  * @param  *buff: Data buffer to store read data
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to read (1..128)
  * @retval DRESULT: Operation result
  */
DRESULT SD_read(BYTE lun, BYTE *buff, DWORD sector, UINT count)
{
  if (!buff || count == 0U) return RES_PARERR;
  while (count-- > 0U) {
    if (BSP_SD_ReadBlocks((uint32_t *)s_sectorBuffer, (uint32_t)sector,
                          1U, SD_DATATIMEOUT) != MSD_OK ||
        SD_WaitCardReady() != RES_OK) {
      sd_report_read_error(sector);
      (void)HAL_SD_Abort(&hsd);   /* 读超时后外设可能卡在 BUSY，主动中止恢复 */
      return RES_ERROR;
    }
    s_sdReadErrs = 0U;            /* 本扇区成功 → 重置，下次错误仍会立即打印 */
    memcpy(buff, s_sectorBuffer, sizeof(s_sectorBuffer));
    buff += sizeof(s_sectorBuffer);
    sector++;
  }
  return RES_OK;
}

/**
  * @brief  Writes Sector(s)
  * @param  lun : not used
  * @param  *buff: Data to be written
  * @param  sector: Sector address (LBA)
  * @param  count: Number of sectors to write (1..128)
  * @retval DRESULT: Operation result
  */
#if _USE_WRITE == 1
DRESULT SD_write(BYTE lun, const BYTE *buff, DWORD sector, UINT count)
{
  if (!buff || count == 0U) return RES_PARERR;
  while (count-- > 0U) {
    memcpy(s_sectorBuffer, buff, sizeof(s_sectorBuffer));
    if (BSP_SD_WriteBlocks((uint32_t *)s_sectorBuffer, (uint32_t)sector,
                           1U, SD_DATATIMEOUT) != MSD_OK ||
        SD_WaitCardReady() != RES_OK) {
      sd_report_write_error(sector);
      /* 写超时后 SDIO 外设常处于未完成态，后续每次写都会立即失败形成死循环
       * （STA 一直 DTIMEOUT）。主动中止当前传输，把 hsd 复位到 READY，
       * 给下一次写恢复的机会。 */
      (void)HAL_SD_Abort(&hsd);
      return RES_ERROR;
    }
    s_sdWriteErrs = 0U;          /* 本扇区成功 → 重置，下次错误仍会立即打印 */
    buff += sizeof(s_sectorBuffer);
    sector++;
  }
  return RES_OK;
}
#endif /* _USE_WRITE == 1 */

/**
  * @brief  I/O control operation
  * @param  lun : not used
  * @param  cmd: Control code
  * @param  *buff: Buffer to send/receive control data
  * @retval DRESULT: Operation result
  */
#if _USE_IOCTL == 1
DRESULT SD_ioctl(BYTE lun, BYTE cmd, void *buff)
{
  DRESULT res = RES_ERROR;
  BSP_SD_CardInfo CardInfo;
  
  if (Stat & STA_NOINIT) return RES_NOTRDY;
  
  switch (cmd)
  {
  /* Make sure that no pending write process */
  case CTRL_SYNC :
    res = SD_WaitCardReady();
    break;
  
  /* Get number of sectors on the disk (DWORD) */
  case GET_SECTOR_COUNT :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockNbr;
    res = RES_OK;
    break;
  
  /* Get R/W sector size (WORD) */
  case GET_SECTOR_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(WORD*)buff = CardInfo.LogBlockSize;
    res = RES_OK;
    break;
  
  /* Get erase block size in unit of sector (DWORD) */
  case GET_BLOCK_SIZE :
    BSP_SD_GetCardInfo(&CardInfo);
    *(DWORD*)buff = CardInfo.LogBlockSize;
    res = RES_OK;
    break;
  
  default:
    res = RES_PARERR;
  }
  
  return res;
}
#endif /* _USE_IOCTL == 1 */
  
/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/

