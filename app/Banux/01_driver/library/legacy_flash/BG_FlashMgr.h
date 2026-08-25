/**
 * BG_FlashMgr.h - BanGUI Flash管理器 (应用层接口)
 * 
 * 功能:
 *   - 管理多颗Flash芯片 (NOR/NAND)
 *   - 提供分区级别的读写接口
 *   - 自动处理擦除操作
 *   - 线程安全保护
 *   - 简单易用的API
 * 
 * 使用方法:
 *   BG_FlashMgr.Init();
 *   BG_FlashMgr.WriteLooper(offset, data, size);
 *   BG_FlashMgr.ReadLooper(offset, buffer, size);
 */

#ifndef __BG_FLASH_MGR_H__
#define __BG_FLASH_MGR_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 状态定义
 *===========================================================================*/

#define BG_FLASH_OK                 0
#define BG_FLASH_ERROR             -1
#define BG_FLASH_ERROR_PARAM       -2
#define BG_FLASH_ERROR_NOT_INIT    -3
#define BG_FLASH_ERROR_ERASE       -4
#define BG_FLASH_ERROR_WRITE       -5
#define BG_FLASH_ERROR_READ        -6
#define BG_FLASH_ERROR_VERIFY      -7
#define BG_FLASH_ERROR_TIMEOUT     -8
#define BG_FLASH_ERROR_BUSY        -9
#define BG_FLASH_ERROR_NO_SPACE    -10

/*===========================================================================
 * 分区定义
 *===========================================================================*/

/* Flash #0 (8MB NOR Flash) - GPIOA21 */
#define BG_FLASH_PARTITION_SYSTEM_SIZE      (1 * 1024 * 1024)    /* 1MB */
#define BG_FLASH_PARTITION_LOOPER_SIZE      (7 * 1024 * 1024)    /* 7MB */

/* Flash #1 (8MB NOR Flash) - GPIOA23 */
#define BG_FLASH_PARTITION_STORAGE_SIZE     (8 * 1024 * 1024)    /* 8MB */

/* 扇区/块大小 */
#define BG_FLASH_SECTOR_SIZE                4096                  /* 4KB */
#define BG_FLASH_BLOCK_SIZE                 (64 * 1024)           /* 64KB */
#define BG_FLASH_PAGE_SIZE                  256

/*===========================================================================
 * 设备状态
 *===========================================================================*/

typedef struct {
    bool initialized;           /* 初始化标志 */
    bool ready;                 /* 设备就绪 */
    uint8_t device_id;          /* 设备ID */
    uint32_t total_size;        /* 总容量(字节) */
    uint32_t used_size;         /* 已使用空间 */
    uint32_t error_count;       /* 错误计数 */
} BG_FlashDeviceStatus_t;

typedef struct {
    BG_FlashDeviceStatus_t flash0;   /* 系统Flash状态 */
    BG_FlashDeviceStatus_t flash1;   /* 存储Flash状态 */
    bool mutex_initialized;          /* 互斥锁初始化标志 */
} BG_FlashMgrStatus_t;

/*===========================================================================
 * BG_FlashMgr 接口结构体
 *===========================================================================*/

typedef struct {
    /* 初始化与反初始化 */
    int32_t (*Init)(void);
    void (*DeInit)(void);
    
    /* 系统分区操作 (Flash #0 前1MB) */
    int32_t (*ReadSystem)(uint32_t offset, uint8_t *buffer, uint32_t size);
    int32_t (*WriteSystem)(uint32_t offset, const uint8_t *data, uint32_t size);
    int32_t (*EraseSystemSector)(uint32_t offset);
    
    /* Looper分区操作 (Flash #0 后7MB) */
    int32_t (*ReadLooper)(uint32_t offset, uint8_t *buffer, uint32_t size);
    int32_t (*WriteLooper)(uint32_t offset, const uint8_t *data, uint32_t size);
    int32_t (*EraseLooperSector)(uint32_t offset);
    int32_t (*EraseLooperBlock)(uint32_t offset);
    int32_t (*EraseLooperAll)(void);
    
    /* 存储分区操作 (Flash #1 全部8MB) */
    int32_t (*ReadStorage)(uint32_t offset, uint8_t *buffer, uint32_t size);
    int32_t (*WriteStorage)(uint32_t offset, const uint8_t *data, uint32_t size);
    int32_t (*EraseStorageSector)(uint32_t offset);
    int32_t (*EraseStorageBlock)(uint32_t offset);
    int32_t (*EraseStorageAll)(void);
    
    /* 状态查询 */
    int32_t (*GetStatus)(BG_FlashMgrStatus_t *status);
    bool (*IsReady)(void);
    uint32_t (*GetLooperFreeSpace)(void);
    uint32_t (*GetStorageFreeSpace)(void);
    
    /* 测试与调试 */
    int32_t (*TestDevice)(uint8_t device_id);
    void (*PrintInfo)(void);
    int32_t (*Format)(uint8_t device_id);  /* 格式化设备 */
    
} BG_FlashMgr_t;

/*===========================================================================
 * 全局实例
 *===========================================================================*/

extern BG_FlashMgr_t BG_FlashMgr;

/*===========================================================================
 * 便捷宏定义 (可选使用)
 *===========================================================================*/

#define BG_FLASH_INIT()                 BG_FlashMgr.Init()
#define BG_FLASH_DEINIT()               BG_FlashMgr.DeInit()

#define BG_FLASH_READ_LOOPER(o,b,s)     BG_FlashMgr.ReadLooper(o,b,s)
#define BG_FLASH_WRITE_LOOPER(o,d,s)    BG_FlashMgr.WriteLooper(o,d,s)
#define BG_FLASH_ERASE_LOOPER(o)        BG_FlashMgr.EraseLooperSector(o)

#define BG_FLASH_READ_STORAGE(o,b,s)    BG_FlashMgr.ReadStorage(o,b,s)
#define BG_FLASH_WRITE_STORAGE(o,d,s)   BG_FlashMgr.WriteStorage(o,d,s)
#define BG_FLASH_ERASE_STORAGE(o)       BG_FlashMgr.EraseStorageSector(o)

#define BG_FLASH_IS_READY()             BG_FlashMgr.IsReady()
#define BG_FLASH_PRINT_INFO()           BG_FlashMgr.PrintInfo()

/*===========================================================================
 * 使用示例
 *===========================================================================*/

#if 0
/* 示例1: 初始化与基本读写 */
void example_basic_usage(void)
{
    uint8_t buffer[256];
    
    // 初始化
    if (BG_FlashMgr.Init() != BG_FLASH_OK) {
        DBG("Flash init failed!\n");
        return;
    }
    
    // 擦除Looper分区第一个扇区
    BG_FlashMgr.EraseLooperSector(0);
    
    // 写入数据
    memset(buffer, 0xAA, 256);
    BG_FlashMgr.WriteLooper(0, buffer, 256);
    
    // 读取数据
    memset(buffer, 0, 256);
    BG_FlashMgr.ReadLooper(0, buffer, 256);
    
    // 验证
    {
        int i;
        for (i = 0; i < 256; i++) {
            if (buffer[i] != 0xAA) {
                DBG("Verify failed at %d\n", i);
            }
        }
    }
}

/* 示例2: 使用便捷宏 */
void example_macro_usage(void)
{
    uint8_t data[128] = {0};
    
    BG_FLASH_INIT();
    
    if (BG_FLASH_IS_READY()) {
        BG_FLASH_WRITE_LOOPER(0x1000, data, 128);
        BG_FLASH_READ_LOOPER(0x1000, data, 128);
    }
    
    BG_FLASH_PRINT_INFO();
}

/* 示例3: 状态查询 */
void example_status_query(void)
{
    BG_FlashMgrStatus_t status;
    
    if (BG_FlashMgr.GetStatus(&status) == BG_FLASH_OK) {
        DBG("Flash0: %s, Size=%dMB, Errors=%d\n",
            status.flash0.ready ? "Ready" : "Not Ready",
            status.flash0.total_size / (1024*1024),
            status.flash0.error_count);
            
        DBG("Looper Free: %d KB\n", 
            BG_FlashMgr.GetLooperFreeSpace() / 1024);
    }
}

/* 示例4: 大数据写入 (自动处理页对齐) */
void example_large_write(void)
{
    uint8_t *large_data = malloc(64 * 1024);
    
    // 擦除一个块 (64KB)
    BG_FlashMgr.EraseLooperBlock(0);
    
    // 写入64KB数据 (自动处理页对齐)
    BG_FlashMgr.WriteLooper(0, large_data, 64 * 1024);
    
    free(large_data);
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BG_FLASH_MGR_H__ */
