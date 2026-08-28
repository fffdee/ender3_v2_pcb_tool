/**
 *****************************************************************************
 * @file     drv_sdio.c
 * @brief    SDIO(SD 卡 / FatFs) 设备驱动接入驱动框架
 *
 * 本驱动把 SDIO SD 卡封装为驱动框架设备节点 /driver/sdio/sd：
 *   - init: 检测 SD 卡在位（BSP_SD_IsDetected）并挂载 FatFs（f_mount），
 *           统计总容量与剩余空间（f_getfree），即"接入 cfs(FatFs)"。
 *   - read/write: 以块为单位访问 block 参数指定的块（默认 0 号块）。
 *****************************************************************************
 */
#include <stdio.h>
#include <string.h>
#include "drv_sdio.h"
#include "drv_fs.h"
#include "drv_device.h"
#include "fs_sd.h"
#include "debug.h"

#include "fatfs.h"          /* SDFatFS / SDPath / retSD */
#include "ff.h"             /* f_mount / f_getfree / FATFS */
#include "diskio.h"
#include "bsp_driver_sd.h"  /* BSP_SD_* */
#include "sdio.h"           /* hsd */

/*===========================================================================
 * 私有数据
 *===========================================================================*/
typedef struct {
    HAL_SD_CardInfoTypeDef cardInfo; /* SD 卡信息（BSP_SD_GetCardInfo） */
    DWORD   totalSectors;            /* 总扇区数（FATFS 统计） */
    DWORD   freeSectors;             /* 剩余扇区数（FATFS 统计） */
    uint32_t blockIndex;             /* 块级 read/write 目标块号 */
    uint8_t  mounted;                /* FatFs 挂载成功标志 */
} SdioPriv_t;

static SdioPriv_t s_sdio;
#if defined(__CC_ARM)
__align(4) static uint8_t s_blockBuffer[512];
#elif defined(__GNUC__)
static uint8_t s_blockBuffer[512] __attribute__((aligned(4)));
#else
static uint8_t s_blockBuffer[512];
#endif

/*===========================================================================
 * 参数读取回调
 *===========================================================================*/
static const char *sd_type_name(uint32_t type)
{
    switch (type) {
        case CARD_SDSC:      return "SDSC";
        case CARD_SDHC_SDXC: return "SDHC/SDXC";
        case CARD_SECURED:   return "SECURED";
        default:             return "UNKNOWN";
    }
}

static int get_detected(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    (void)p;
    return snprintf(buf, maxLen, "%d", BSP_SD_IsDetected() ? 1 : 0);
}

static int get_type(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    return snprintf(buf, maxLen, "%s", sd_type_name(p->cardInfo.CardType));
}

static int get_backend(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "sd");
}

static int get_size_mb(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    uint32_t mb = (uint32_t)((p->totalSectors * 512u) >> 20);
    return snprintf(buf, maxLen, "%lu", (unsigned long)mb);
}

static int get_free_mb(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    uint32_t mb = (uint32_t)((p->freeSectors * 512u) >> 20);
    return snprintf(buf, maxLen, "%lu", (unsigned long)mb);
}

static int get_size_kb(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)(p->totalSectors / 2u));
}

static int get_free_kb(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)(p->freeSectors / 2u));
}

static int get_mount(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    return snprintf(buf, maxLen, "%d", p->mounted ? 1 : 0);
}

static int get_block(char *buf, uint16_t maxLen, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    return snprintf(buf, maxLen, "%lu", (unsigned long)p->blockIndex);
}

static int set_block(const char *value, void *userData)
{
    SdioPriv_t *p = (SdioPriv_t *)userData;
    unsigned long v;
    if (!value || sscanf(value, "%lu", &v) != 1) {
        return -1;
    }
    p->blockIndex = (uint32_t)v;
    return 0;
}

/*===========================================================================
 * 驱动操作接口
 *===========================================================================*/
static int sdio_drv_init(void *priv)
{
    SdioPriv_t *p = (SdioPriv_t *)priv;
    FATFS *fs = &SDFatFS;
    DWORD  freClust = 0;
    FRESULT fres;

    p->mounted     = 0;
    p->totalSectors = 0;
    p->freeSectors  = 0;
    memset(&p->cardInfo, 0, sizeof(p->cardInfo));

    if (!BSP_SD_IsDetected()) {
        DBG("[SDIO] card not present, device not registered\n");
        return -1;
    }

    /* SD 始终使用逻辑盘 0:，内部 Flash 是独立的逻辑盘 1:。 */
    fres = f_mount(&SDFatFS, SDPath, 1);
    if (fres != FR_OK) {
        DBG("[SDIO] f_mount failed: %d, device not registered\n", (int)fres);
        /* 打印 HAL SD 错误码定位失败阶段（CMD0 无响应/电压窗口/CSD 解析等） */
        DBG("[SDIO] hsd.ErrorCode=0x%08lX, CardType=%lu, State=%lu\n",
            (unsigned long)hsd.ErrorCode,
            (unsigned long)hsd.SdCard.CardType,
            (unsigned long)hsd.State);
        /* 打印外设寄存器区分根因：
         * STA.DTIMEOUT(bit3) / DCRCFAIL(bit1) / RXOVERR(bit5) 为数据错误；
         * CLKCR.NEGEDGE(bit13) 为采样沿，WIDBUS[12:11] 为总线宽度。 */
        DBG("[SDIO] STA=0x%08lX CLKCR=0x%08lX DCTRL=0x%08lX edge=%s\n",
            (unsigned long)SDIO->STA, (unsigned long)SDIO->CLKCR,
            (unsigned long)SDIO->DCTRL,
            (hsd.Init.ClockEdge == SDIO_CLOCK_EDGE_FALLING) ? "falling" : "rising");
        /* 仍尝试读取卡信息，便于诊断 */
        BSP_SD_GetCardInfo(&p->cardInfo);
        return -1;
    }
    p->mounted = 1;
    BSP_SD_GetCardInfo(&p->cardInfo);

    /* 3. 统计容量：总簇 = n_fatent - 2，剩余由 f_getfree 返回 */
    if (f_getfree(SDPath, &freClust, &fs) == FR_OK && fs) {
        p->totalSectors = (fs->n_fatent - 2) * fs->csize;
        p->freeSectors  = freClust * fs->csize;
    }

    DBG("[SDIO] mounted ok, total=%lu KB, free=%lu KB, edge=%s, bus=%s\n",
        (unsigned long)(p->totalSectors / 2u),
        (unsigned long)(p->freeSectors / 2u),
        hsd.Init.ClockEdge == SDIO_CLOCK_EDGE_FALLING ? "falling" : "rising",
        ((SDIO->CLKCR & SDIO_CLKCR_WIDBUS) == 0u) ? "1-bit" : "4-bit");
    return 0;
}

static int sdio_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    SdioPriv_t *p = (SdioPriv_t *)priv;
    uint32_t blkSize;

    if (!buf || len == 0 || !p->mounted) {
        return -1;
    }
    blkSize = (p->cardInfo.BlockSize) ? p->cardInfo.BlockSize : 512u;
    if (disk_read(0u, s_blockBuffer, p->blockIndex, 1u) != RES_OK) {
        return -1;
    }
    if (len > blkSize) {
        len = blkSize;
    }
    memcpy(buf, s_blockBuffer, len);
    return (int)len;
}

static int sdio_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    SdioPriv_t *p = (SdioPriv_t *)priv;
    uint32_t blkSize;

    if (!buf || len == 0 || !p->mounted) {
        return -1;
    }
    blkSize = (p->cardInfo.BlockSize) ? p->cardInfo.BlockSize : 512u;
    if (len > blkSize) {
        len = blkSize;
    }
    memset(s_blockBuffer, 0xFF, sizeof(s_blockBuffer));
    memcpy(s_blockBuffer, buf, len);
    if (disk_write(0u, s_blockBuffer, p->blockIndex, 1u) != RES_OK) {
        return -1;
    }
    return (int)len;
}

/*===========================================================================
 * 参数定义与设备注册
 *===========================================================================*/
static const FsParamDef_t sdio_params[] = {
    FS_PARAM_DEF("detected", "SD card present (0/1)",               get_detected, NULL),
    FS_PARAM_DEF("backend",  "active FAT block backend",           get_backend,  NULL),
    FS_PARAM_DEF("type",     "SD card type (SDSC/SDHC/MMC)",        get_type,     NULL),
    FS_PARAM_DEF("size_mb",  "total capacity in MB",                get_size_mb,  NULL),
    FS_PARAM_DEF("free_mb",  "free space in MB (FATFS)",            get_free_mb,  NULL),
    FS_PARAM_DEF("size_kb",  "total capacity in KB",                get_size_kb,  NULL),
    FS_PARAM_DEF("free_kb",  "free space in KB",                    get_free_kb,  NULL),
    FS_PARAM_DEF("mount",    "FATFS mounted (0/1)",                 get_mount,    NULL),
    FS_PARAM_DEF("block",    "block index for read/write (RW)",     get_block,    set_block),
    FS_PARAM_END
};

/* 注意：不能用const，因为 DrvDevice_Register 运行时需修改 fsNode/isRegistered 字段 */
static DrvDevice_t sdio_drv = {
    .name     = "sd",
    .desc     = "SDIO SD card / FatFs",
    .bus      = DRV_BUS_SDIO,
    .init     = sdio_drv_init,
    .deinit   = NULL,
    .open     = NULL,
    .close    = NULL,
    .read     = sdio_drv_read,
    .write    = sdio_drv_write,
    .ioctl    = NULL,
    .params   = sdio_params,
    .privData = &s_sdio,
};

int DrvSdio_IsMounted(void)
{
    return s_sdio.mounted ? 1 : 0;
}

int DrvSdio_Register(void)
{
    int ret = DrvDevice_Register((DrvDevice_t *)&sdio_drv);

    /* 驱动初始化成功且 FatFs 已挂载 -> 将 SD 卡文件系统挂载到 VFS /sd */
    if (ret == 0 && s_sdio.mounted) {
        SdFs_Mount();
    }
    return ret;
}
