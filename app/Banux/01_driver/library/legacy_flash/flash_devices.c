/**
 * flash_devices.c - Flash设备注册和管理实现
 */

#include "flash_devices.h"
#include "flash_nor_w25qxx.h"
#include "flash_nand_w25n02.h"
#include "psram_esp64h.h"
#include "sd_card_driver.h"
#include "debug.h"
#include "gpio.h"
#include <string.h>

/*===========================================================================
 * CS引脚控制函数
 *===========================================================================*/

/* Flash #0 CS控制 */
#if HW_FLASH0_EN
static void flash0_cs_init(void)
{
    /* 配置为GPIO输出模式，初始为高电平（未选中） */
    GPIO_RegOneBitClear(GPIO_A_IE, FLASH0_CS_GPIO_MASK);   /* 关闭输入 */
    GPIO_RegOneBitSet(GPIO_A_OE, FLASH0_CS_GPIO_MASK);     /* 使能输出 */
    GPIO_RegOneBitSet(GPIO_A_OUT, FLASH0_CS_GPIO_MASK);    /* 输出高电平 */
}

static void flash0_cs_select(void)
{
    GPIO_RegOneBitClear(GPIO_A_OUT, FLASH0_CS_GPIO_MASK);  /* 输出低电平 */
}

static void flash0_cs_deselect(void)
{
    GPIO_RegOneBitSet(GPIO_A_OUT, FLASH0_CS_GPIO_MASK);    /* 输出高电平 */
}
#endif /* HW_FLASH0_EN */

/* Flash #1 CS控制 */
#if HW_FLASH1_EN
static void flash1_cs_init(void)
{
    /* 配置为GPIO输出模式，初始为高电平（未选中） */
    GPIO_RegOneBitClear(GPIO_A_IE, FLASH1_CS_GPIO_MASK);   /* 关闭输入 */
    GPIO_RegOneBitSet(GPIO_A_OE, FLASH1_CS_GPIO_MASK);     /* 使能输出 */
    GPIO_RegOneBitSet(GPIO_A_OUT, FLASH1_CS_GPIO_MASK);    /* 输出高电平 */
}

static void flash1_cs_select(void)
{
    GPIO_RegOneBitClear(GPIO_A_OUT, FLASH1_CS_GPIO_MASK);
}

static void flash1_cs_deselect(void)
{
    GPIO_RegOneBitSet(GPIO_A_OUT, FLASH1_CS_GPIO_MASK);
}
#endif /* HW_FLASH1_EN */

/* NAND Flash (W25N02) CS控制 */
#if HW_NAND0_EN
static void nand0_cs_init(void)
{
    GPIO_RegOneBitClear(GPIO_A_IE,  NAND0_CS_GPIO_MASK);
    GPIO_RegOneBitSet(GPIO_A_OE,    NAND0_CS_GPIO_MASK);
    GPIO_RegOneBitSet(GPIO_A_OUT,   NAND0_CS_GPIO_MASK); /* 默认高电平（未选中） */
}

static void nand0_cs_select(void)
{
    GPIO_RegOneBitClear(GPIO_A_OUT, NAND0_CS_GPIO_MASK);
}

static void nand0_cs_deselect(void)
{
    GPIO_RegOneBitSet(GPIO_A_OUT, NAND0_CS_GPIO_MASK);
}
#endif /* HW_NAND0_EN */

/* PSRAM (ESP-PSRAM64H) CS控制 */
#if HW_PSRAM0_EN
static void psram0_cs_init(void)
{
    GPIO_RegOneBitClear(HW_PSRAM0_CS_GPIO_IE,  PSRAM0_CS_GPIO_MASK);
    GPIO_RegOneBitSet(HW_PSRAM0_CS_GPIO_OE,    PSRAM0_CS_GPIO_MASK);
    GPIO_RegOneBitSet(HW_PSRAM0_CS_GPIO_OUT,   PSRAM0_CS_GPIO_MASK); /* 默认高电平（未选中） */
}

static void psram0_cs_select(void)
{
    GPIO_RegOneBitClear(HW_PSRAM0_CS_GPIO_OUT, PSRAM0_CS_GPIO_MASK);
}

static void psram0_cs_deselect(void)
{
    GPIO_RegOneBitSet(HW_PSRAM0_CS_GPIO_OUT, PSRAM0_CS_GPIO_MASK);
}
#endif /* HW_PSRAM0_EN */

/*===========================================================================
 * 设备实例
 *===========================================================================*/

static FlashDevice_t *g_flash0 = NULL;  /* 系统Flash - NOR  */
static FlashDevice_t *g_flash1 = NULL;  /* 存储Flash - NOR  */
static FlashDevice_t *g_nand0  = NULL;  /* NAND Flash - W25N02 */
static FlashDevice_t *g_psram0 = NULL;  /* PSRAM - ESP-PSRAM64H */
static FlashDevice_t *g_sdcard0 = NULL; /* SD Card - SDIO */
static bool g_devices_initialized = false;

/*===========================================================================
 * 设备初始化
 *===========================================================================*/

FlashStatus_t FlashDevices_Init(void)
{
    FlashStatus_t ret;
    
    if (g_devices_initialized) {
        return FLASH_OK;
    }
    
    DBG("[FlashDevices] Initializing...\n");
    
    /* 初始化总线 */
    FlashBus_Init();
    
    /* 创建Flash #0 (系统Flash) */
#if HW_FLASH0_EN
    g_flash0 = W25Qxx_Create("flash0_sys",
                             flash0_cs_select,
                             flash0_cs_deselect,
                             flash0_cs_init);
    if (!g_flash0) {
        DBG("[FlashDevices] Failed to create flash0\n");
        return FLASH_ERR_NOMEM;
    }
    
    /* 注册到总线 */
    ret = FlashBus_Register(g_flash0);
    if (ret != FLASH_OK) {
        DBG("[FlashDevices] Failed to register flash0\n");
        W25Qxx_Destroy(g_flash0);
        g_flash0 = NULL;
        return ret;
    }
    
    /* 初始化设备 */
    ret = FlashDev_Init(g_flash0);
    if (ret != FLASH_OK) {
        DBG("[FlashDevices] Failed to init flash0\n");
        /* 继续执行，设备可能暂时离线 */
    }
#endif /* HW_FLASH0_EN */
    
    /* 创建Flash #1 (存储Flash) - 仅旧板子有两片 NOR Flash */
#if HW_FLASH1_EN
    g_flash1 = W25Qxx_Create("flash1_stor",
                             flash1_cs_select,
                             flash1_cs_deselect,
                             flash1_cs_init);
    if (g_flash1) {
        ret = FlashBus_Register(g_flash1);
        if (ret == FLASH_OK) {
            ret = FlashDev_Init(g_flash1);
            if (ret != FLASH_OK) {
                DBG("[FlashDevices] Flash1 init failed (may not present)\n");
            }
        }
    }
#endif /* HW_FLASH1_EN */

    /* 创建NAND Flash (W25N02) */
#if HW_NAND0_EN
    g_nand0 = W25N02_Create("nand0",
                            nand0_cs_select,
                            nand0_cs_deselect,
                            nand0_cs_init);
    if (g_nand0) {
        ret = FlashBus_Register(g_nand0);
        if (ret == FLASH_OK) {
            ret = FlashDev_Init(g_nand0);
            if (ret == FLASH_OK) {
                DBG("[FlashDevices] NAND0 (W25N02) init OK\n");
                /* 异步扫描坏块表（先用 RAM 内的空表，待命令触发或任务批完成后再扫） */
                DBG("[FlashDevices] Run 'flash -b' to scan bad blocks\n");
            } else {
                DBG("[FlashDevices] NAND0 init failed (ret=%d)\n", ret);
            }
        } else {
            DBG("[FlashDevices] NAND0 register failed\n");
        }
    } else {
        DBG("[FlashDevices] Failed to create nand0\n");
    }
#endif /* HW_NAND0_EN */

    /* 创建PSRAM (ESP-PSRAM64H) */
#if HW_PSRAM0_EN
    g_psram0 = PSRAM64H_Create("psram0",
                               psram0_cs_select,
                               psram0_cs_deselect,
                               psram0_cs_init);
    if (g_psram0) {
        ret = FlashBus_Register(g_psram0);
        if (ret == FLASH_OK) {
            ret = FlashDev_Init(g_psram0);
            if (ret == FLASH_OK) {
                DBG("[FlashDevices] PSRAM0 (ESP-PSRAM64H) init OK\n");
            } else {
                DBG("[FlashDevices] PSRAM0 init failed (ret=%d)\n", ret);
            }
        } else {
            DBG("[FlashDevices] PSRAM0 register failed\n");
        }
    } else {
        DBG("[FlashDevices] Failed to create psram0\n");
    }
#endif /* HW_PSRAM0_EN */

    /* 创建SD Card (SDIO接口) */
#if HW_SDCARD0_EN
    g_sdcard0 = SDCard_Create("sdcard0");
    if (g_sdcard0) {
        ret = FlashBus_Register(g_sdcard0);
        if (ret == FLASH_OK) {
            ret = FlashDev_Init(g_sdcard0);
            if (ret == FLASH_OK) {
                DBG("[FlashDevices] SDCARD0 (SDIO) init OK\n");
            } else {
                DBG("[FlashDevices] SDCARD0 init failed (ret=%d, may not present)\n", ret);
            }
        } else {
            DBG("[FlashDevices] SDCARD0 register failed\n");
        }
    } else {
        DBG("[FlashDevices] Failed to create sdcard0\n");
    }
#endif /* HW_SDCARD0_EN */
    
    /* 打印设备信息 */
    FlashBus_PrintInfo();
    
    g_devices_initialized = true;
    DBG("[FlashDevices] Initialized\n");
    return FLASH_OK;
}

void FlashDevices_DeInit(void)
{
    if (!g_devices_initialized) {
        return;
    }
    
    if (g_flash1) {
        FlashBus_Unregister(g_flash1);
        W25Qxx_Destroy(g_flash1);
        g_flash1 = NULL;
    }

#if HW_NAND0_EN
    if (g_nand0) {
        FlashBus_Unregister(g_nand0);
        W25N02_Destroy(g_nand0);
        g_nand0 = NULL;
    }
#endif

#if HW_PSRAM0_EN
    if (g_psram0) {
        FlashBus_Unregister(g_psram0);
        PSRAM64H_Destroy(g_psram0);
        g_psram0 = NULL;
    }
#endif

#if HW_SDCARD0_EN
    if (g_sdcard0) {
        FlashBus_Unregister(g_sdcard0);
        SDCard_Destroy(g_sdcard0);
        g_sdcard0 = NULL;
    }
#endif /* HW_SDCARD0_EN */
    
#if HW_FLASH0_EN
    if (g_flash0) {
        FlashBus_Unregister(g_flash0);
        W25Qxx_Destroy(g_flash0);
        g_flash0 = NULL;
    }
#endif /* HW_FLASH0_EN */
    
    FlashBus_DeInit();
    
    g_devices_initialized = false;
    DBG("[FlashDevices] DeInitialized\n");
}

FlashDevice_t* FlashDevices_GetSystemFlash(void)
{
    return g_flash0;
}

FlashDevice_t* FlashDevices_GetStorageFlash(void)
{
    return g_flash1;
}

FlashDevice_t* FlashDevices_GetNandFlash(void)
{
    return g_nand0;
}

FlashDevice_t* FlashDevices_GetPsramFlash(void)
{
    return g_psram0;
}

FlashDevice_t* FlashDevices_GetSDCardFlash(void)
{
    return g_sdcard0;
}

FlashDevice_t* FlashDevices_GetDevice(uint8_t dev_id)
{
    switch (dev_id) {
        case 0: return g_flash0;
        case 1: return g_flash1;
        case 2: return g_nand0;
        case 3: return g_psram0;
        case 4: return g_sdcard0;
        default: return NULL;
    }
}

/*===========================================================================
 * Shell命令
 *===========================================================================*/

void FlashDevices_RegisterShellCommands(void)
{
    /* Shell命令通过 FlashBus_ShellCmd 注册 */
    /* 在shell_commands.c中添加:
     *   {"flash", FlashBus_ShellCmd, "Flash operations"}
     */
    DBG("[FlashDevices] Shell commands: use 'flash' command\n");
}

/*===========================================================================
 * 分区操作实现
 *===========================================================================*/

/* 系统分区 (Flash#0 前1MB) */
FlashStatus_t FlashPartition_SystemRead(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH0_PARTITION_SYSTEM_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Read(g_flash0, FLASH0_PARTITION_SYSTEM_START + offset, buf, len);
}

FlashStatus_t FlashPartition_SystemWrite(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH0_PARTITION_SYSTEM_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Write(g_flash0, FLASH0_PARTITION_SYSTEM_START + offset, buf, len);
}

FlashStatus_t FlashPartition_SystemEraseSector(uint32_t offset)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH0_PARTITION_SYSTEM_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseSector(g_flash0, FLASH0_PARTITION_SYSTEM_START + offset);
}

/* Looper分区 (Flash#0 全部8MB) */
FlashStatus_t FlashPartition_LooperRead(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH0_PARTITION_LOOPER_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Read(g_flash0, FLASH0_PARTITION_LOOPER_START + offset, buf, len);
}

FlashStatus_t FlashPartition_LooperWrite(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH0_PARTITION_LOOPER_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Write(g_flash0, FLASH0_PARTITION_LOOPER_START + offset, buf, len);
}

FlashStatus_t FlashPartition_LooperEraseSector(uint32_t offset)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH0_PARTITION_LOOPER_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseSector(g_flash0, FLASH0_PARTITION_LOOPER_START + offset);
}

FlashStatus_t FlashPartition_LooperEraseBlock(uint32_t offset)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH0_PARTITION_LOOPER_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseBlock(g_flash0, FLASH0_PARTITION_LOOPER_START + offset);
}

/* Looper整片擦除 - 录制前一次性擦除整颗Flash0 (~20s，建议在任务中异步执行) */
FlashStatus_t FlashPartition_LooperEraseChip(void)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    DBG("[Flash] Looper chip erase start (flash0 8MB)...\n");
    FlashStatus_t ret = FlashDev_EraseChip(g_flash0);
    if (ret == FLASH_OK) {
        DBG("[Flash] Looper chip erase done\n");
    } else {
        DBG("[Flash] Looper chip erase FAILED: %d\n", ret);
    }
    return ret;
}

/**
 * @brief 发送全片擦除命令后立即返回（非阻塞）
 *
 * 命令发出后芯片即开始内部擦除（典型 20~100 s）。
 * 调用方须循环调用 FlashPartition_LooperIsErasing() 等待完成，
 * 在此期间禁止对 flash0 做任何读写/擦除操作。
 */
FlashStatus_t FlashPartition_LooperEraseChipAsync(void)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    DBG("[Flash] Looper async chip erase start\n");
    return W25Qxx_EraseChipStart(g_flash0);
}

/**
 * @brief 查询全片擦除是否仍在进行（非阻塞）
 *
 * @return 1 = 仍在擦除；0 = 擦除完成（或设备无效）
 */
uint8_t FlashPartition_LooperIsErasing(void)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return 0;
    }
    return W25Qxx_IsBusy(g_flash0);
}

/* 存储分区 (Flash#1 全部8MB) */
FlashStatus_t FlashPartition_StorageRead(uint32_t offset, uint8_t *buf, uint32_t len)
{
    if (!g_flash1 || !g_flash1->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH1_PARTITION_STORAGE_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Read(g_flash1, FLASH1_PARTITION_STORAGE_START + offset, buf, len);
}

FlashStatus_t FlashPartition_StorageWrite(uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if (!g_flash1 || !g_flash1->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > FLASH1_PARTITION_STORAGE_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Write(g_flash1, FLASH1_PARTITION_STORAGE_START + offset, buf, len);
}

FlashStatus_t FlashPartition_StorageEraseSector(uint32_t offset)
{
    if (!g_flash1 || !g_flash1->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH1_PARTITION_STORAGE_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseSector(g_flash1, FLASH1_PARTITION_STORAGE_START + offset);
}

FlashStatus_t FlashPartition_StorageEraseBlock(uint32_t offset)
{
    if (!g_flash1 || !g_flash1->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH1_PARTITION_STORAGE_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseBlock(g_flash1, FLASH1_PARTITION_STORAGE_START + offset);
}
/*===========================================================================
 * 多Flash Looper分区操作实现 (按dev_id)
 *===========================================================================*/

FlashStatus_t FlashPartition_LooperReadByDev(uint8_t dev_id, uint32_t offset, uint8_t *buf, uint32_t len)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > LOOPER_FLASH_DEV_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Read(dev, offset, buf, len);
}

FlashStatus_t FlashPartition_LooperWriteByDev(uint8_t dev_id, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset + len > LOOPER_FLASH_DEV_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_Write(dev, offset, buf, len);
}

FlashStatus_t FlashPartition_LooperEraseSectorByDev(uint8_t dev_id, uint32_t offset)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= LOOPER_FLASH_DEV_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return FlashDev_EraseSector(dev, offset);
}

FlashStatus_t FlashPartition_LooperEraseChipByDev(uint8_t dev_id)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    DBG("[Flash] Looper chip erase start (dev%d)...\n", dev_id);
    FlashStatus_t ret = FlashDev_EraseChip(dev);
    DBG("[Flash] Looper chip erase %s (dev%d)\n", ret == FLASH_OK ? "done" : "FAILED", dev_id);
    return ret;
}

FlashStatus_t FlashPartition_LooperEraseChipAsyncByDev(uint8_t dev_id)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    DBG("[Flash] Looper async chip erase start (dev%d)\n", dev_id);
    return W25Qxx_EraseChipStart(dev);
}

uint8_t FlashPartition_LooperIsErasingByDev(uint8_t dev_id)
{
    FlashDevice_t *dev = FlashDevices_GetDevice(dev_id);
    if (!dev || !dev->initialized) {
        return 0;
    }
    return W25Qxx_IsBusy(dev);
}

/* Looper 分区 — 64KB 块非阻塞擦除（单 Flash#0） */
FlashStatus_t FlashPartition_LooperEraseBlockAsync(uint32_t offset)
{
    if (!g_flash0 || !g_flash0->initialized) {
        return FLASH_ERR_NOT_INIT;
    }
    if (offset >= FLASH0_PARTITION_LOOPER_SIZE) {
        return FLASH_ERR_PARAM;
    }
    return W25Qxx_EraseBlockStart(g_flash0, FLASH0_PARTITION_LOOPER_START + offset);
}