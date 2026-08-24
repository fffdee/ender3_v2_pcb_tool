/**
 * flash_manager.h - Flash管理层
 * 
 * 管理多颗Flash芯片的分区和访问：
 * - Flash #0 (W25Q64): 前1MB系统设置 + 后7MB给Looper
 * - Flash #1 (W25Q64): 8MB纯存储
 */

#ifndef __FLASH_MANAGER_H__
#define __FLASH_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "flash_driver.h"

/*===========================================================================
 * 分区定义
 *===========================================================================*/

/* Flash设备ID */
#define FLASH_DEV_0             0   /* 主Flash (系统+Looper) */
#define FLASH_DEV_1             1   /* 存储Flash */
#define FLASH_DEV_MAX           2

/* Flash #0 分区布局 (W25Q64 = 8MB) */
#define PARTITION_SYSTEM_START      0x000000    /* 系统设置起始地址 */
#define PARTITION_SYSTEM_SIZE       0x100000    /* 系统设置大小: 1MB */
#define PARTITION_LOOPER_START      0x100000    /* Looper起始地址 */
#define PARTITION_LOOPER_SIZE       0x700000    /* Looper大小: 7MB */

/* Flash #1 分区布局 (W25Q64 = 8MB) */
#define PARTITION_STORAGE_START     0x000000    /* 存储起始地址 */
#define PARTITION_STORAGE_SIZE      0x800000    /* 存储大小: 8MB */

/* 系统设置分区内部布局 */
#define SETTINGS_MAGIC_ADDR         0x000000    /* 魔术字地址 */
#define SETTINGS_VERSION_ADDR       0x000004    /* 版本号地址 */
#define SETTINGS_DATA_ADDR          0x000100    /* 数据起始地址 */
#define SETTINGS_BACKUP_ADDR        0x080000    /* 备份区起始地址 (512KB) */
#define SETTINGS_MAGIC_VALUE        0x42475346  /* "BGSF" */

/*===========================================================================
 * 分区类型
 *===========================================================================*/

typedef enum {
    PARTITION_TYPE_SYSTEM = 0,  /* 系统设置分区 */
    PARTITION_TYPE_LOOPER,      /* Looper分区 */
    PARTITION_TYPE_STORAGE,     /* 通用存储分区 */
    PARTITION_TYPE_MAX
} PartitionType_t;

/*===========================================================================
 * 分区信息结构
 *===========================================================================*/

typedef struct {
    PartitionType_t type;       /* 分区类型 */
    uint8_t flash_id;           /* 所属Flash设备ID */
    uint32_t start_addr;        /* 分区起始地址 */
    uint32_t size;              /* 分区大小 */
    const char *name;           /* 分区名称 */
} PartitionInfo_t;

/*===========================================================================
 * Flash管理器状态
 *===========================================================================*/

typedef struct {
    bool initialized;                       /* 是否已初始化 */
    FlashDriver_t *flash[FLASH_DEV_MAX];    /* Flash驱动实例 */
    PartitionInfo_t partitions[PARTITION_TYPE_MAX]; /* 分区信息 */
} FlashManager_t;

/*===========================================================================
 * API函数
 *===========================================================================*/

/**
 * 初始化Flash管理器
 * @return FLASH_OK成功，其他失败
 */
FlashStatus_t FlashManager_Init(void);

/**
 * 反初始化Flash管理器
 */
void FlashManager_DeInit(void);

/**
 * 获取Flash管理器实例
 * @return 管理器指针
 */
FlashManager_t* FlashManager_GetInstance(void);

/**
 * 获取指定Flash设备
 * @param flash_id Flash设备ID
 * @return 驱动指针，失败返回NULL
 */
FlashDriver_t* FlashManager_GetFlash(uint8_t flash_id);

/**
 * 获取分区信息
 * @param type 分区类型
 * @return 分区信息指针，失败返回NULL
 */
const PartitionInfo_t* FlashManager_GetPartition(PartitionType_t type);

/*===========================================================================
 * 分区读写API
 *===========================================================================*/

/**
 * 从分区读取数据
 * @param type 分区类型
 * @param offset 分区内偏移
 * @param buf 数据缓冲区
 * @param len 读取长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_Read(PartitionType_t type, uint32_t offset, uint8_t *buf, uint32_t len);

/**
 * 写入数据到分区
 * @param type 分区类型
 * @param offset 分区内偏移
 * @param buf 数据缓冲区
 * @param len 写入长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_Write(PartitionType_t type, uint32_t offset, const uint8_t *buf, uint32_t len);

/**
 * 擦除分区扇区
 * @param type 分区类型
 * @param offset 分区内偏移（会对齐到扇区边界）
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_EraseSector(PartitionType_t type, uint32_t offset);

/**
 * 擦除整个分区
 * @param type 分区类型
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_ErasePartition(PartitionType_t type);

/*===========================================================================
 * 系统设置API
 *===========================================================================*/

/**
 * 读取系统设置
 * @param key 设置键值（偏移地址）
 * @param buf 数据缓冲区
 * @param len 读取长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_ReadSettings(uint32_t key, uint8_t *buf, uint32_t len);

/**
 * 写入系统设置
 * @param key 设置键值（偏移地址）
 * @param buf 数据缓冲区
 * @param len 写入长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_WriteSettings(uint32_t key, const uint8_t *buf, uint32_t len);

/**
 * 检查系统设置是否有效
 * @return true有效，false无效
 */
bool FlashManager_IsSettingsValid(void);

/**
 * 初始化系统设置（首次使用）
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_InitSettings(void);

/*===========================================================================
 * Looper专用API
 *===========================================================================*/

/**
 * 读取Looper数据
 * @param offset Looper分区内偏移
 * @param buf 数据缓冲区
 * @param len 读取长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_LooperRead(uint32_t offset, uint8_t *buf, uint32_t len);

/**
 * 写入Looper数据
 * @param offset Looper分区内偏移
 * @param buf 数据缓冲区
 * @param len 写入长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_LooperWrite(uint32_t offset, const uint8_t *buf, uint32_t len);

/**
 * 擦除Looper扇区
 * @param offset Looper分区内偏移
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_LooperEraseSector(uint32_t offset);

/**
 * 获取Looper分区大小
 * @return 分区大小（字节）
 */
uint32_t FlashManager_LooperGetSize(void);

/*===========================================================================
 * 存储分区API
 *===========================================================================*/

/**
 * 读取存储数据
 * @param offset 存储分区内偏移
 * @param buf 数据缓冲区
 * @param len 读取长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_StorageRead(uint32_t offset, uint8_t *buf, uint32_t len);

/**
 * 写入存储数据
 * @param offset 存储分区内偏移
 * @param buf 数据缓冲区
 * @param len 写入长度
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_StorageWrite(uint32_t offset, const uint8_t *buf, uint32_t len);

/**
 * 擦除存储扇区
 * @param offset 存储分区内偏移
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_StorageEraseSector(uint32_t offset);

/**
 * 获取存储分区大小
 * @return 分区大小（字节）
 */
uint32_t FlashManager_StorageGetSize(void);

/*===========================================================================
 * 调试和测试
 *===========================================================================*/

/**
 * 打印Flash管理器信息
 */
void FlashManager_PrintInfo(void);

/**
 * 测试Flash读写
 * @param flash_id Flash设备ID
 * @return FLASH_OK成功
 */
FlashStatus_t FlashManager_Test(uint8_t flash_id);

#endif /* __FLASH_MANAGER_H__ */
