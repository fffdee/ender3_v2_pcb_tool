/**
 * BG_FlashMgr.c - BanGUI Flash管理器 (应用层实现)
 */

#include "BG_FlashMgr.h"
#include "flash_devices.h"
#include "flash_bus.h"
#include "debug.h"
#include "rtos_api.h"
#include <string.h>
#include <stdlib.h>

/*===========================================================================
 * 内部宏定义
 *===========================================================================*/

#define BG_FLASHMGR_DEBUG   1

#if BG_FLASHMGR_DEBUG
    #define FLASHMGR_LOG(fmt, ...)  DBG("[BG_FlashMgr] " fmt, ##__VA_ARGS__)
#else
    #define FLASHMGR_LOG(...)
#endif

/*===========================================================================
 * 内部变量
 *===========================================================================*/

static BG_FlashMgrStatus_t g_flash_status = {0};
static SemaphoreHandle_t g_flash_mutex = NULL;
static bool g_initialized = false;

/*===========================================================================
 * 互斥锁操作
 *===========================================================================*/

static inline bool flash_lock(uint32_t timeout_ms)
{
    if (g_flash_mutex) {
        return xSemaphoreTake(g_flash_mutex, (timeout_ms / portTICK_PERIOD_MS)) == pdTRUE;
    }
    return true;  /* 没有互斥锁时直接通过 */
}

static inline void flash_unlock(void)
{
    if (g_flash_mutex) {
        xSemaphoreGive(g_flash_mutex);
    }
}

/*===========================================================================
 * 内部辅助函数
 *===========================================================================*/

/**
 * @brief 更新设备状态
 */
static void update_device_status(FlashDevice_t *dev, BG_FlashDeviceStatus_t *status)
{
    if (!dev || !status) return;
    
    status->initialized = dev->registered;
    status->ready = dev->initialized;
    status->device_id = dev->id;
    status->total_size = dev->info.total_size;
    status->used_size = 0;  /* 需要文件系统支持 */
    /* error_count在操作失败时更新 */
}

/**
 * @brief 检查参数有效性
 */
static inline bool is_valid_looper_range(uint32_t offset, uint32_t size)
{
    return (offset + size) <= BG_FLASH_PARTITION_LOOPER_SIZE;
}

static inline bool is_valid_storage_range(uint32_t offset, uint32_t size)
{
    return (offset + size) <= BG_FLASH_PARTITION_STORAGE_SIZE;
}

static inline bool is_valid_system_range(uint32_t offset, uint32_t size)
{
    return (offset + size) <= BG_FLASH_PARTITION_SYSTEM_SIZE;
}

/*===========================================================================
 * 初始化与反初始化
 *===========================================================================*/

static int32_t bg_flash_init(void)
{
    FlashStatus_t ret;
    
    if (g_initialized) {
        FLASHMGR_LOG("Already initialized\n");
        return BG_FLASH_OK;
    }
    
    FLASHMGR_LOG("Initializing...\n");
    
    /* 创建互斥锁 */
    if (!g_flash_mutex) {
        g_flash_mutex = xSemaphoreCreateMutex();
        if (!g_flash_mutex) {
            FLASHMGR_LOG("Failed to create mutex\n");
            return BG_FLASH_ERROR;
        }
        g_flash_status.mutex_initialized = true;
    }
    
    /* 初始化底层设备 */
    ret = FlashDevices_Init();
    if (ret != FLASH_OK) {
        FLASHMGR_LOG("FlashDevices_Init failed: %d\n", ret);
        return BG_FLASH_ERROR;
    }
    
    /* 更新状态 */
    FlashDevice_t *dev0 = FlashDevices_GetSystemFlash();
    FlashDevice_t *dev1 = FlashDevices_GetStorageFlash();
    
    if (dev0) {
        update_device_status(dev0, &g_flash_status.flash0);
    }
    if (dev1) {
        update_device_status(dev1, &g_flash_status.flash1);
    }
    
    g_initialized = true;
    
    FLASHMGR_LOG("Initialized OK\n");
    FLASHMGR_LOG("  Flash0: %s (ID=%d, Size=%dMB)\n",
                 g_flash_status.flash0.ready ? "Ready" : "Not Ready",
                 g_flash_status.flash0.device_id,
                 g_flash_status.flash0.total_size / (1024*1024));
    FLASHMGR_LOG("  Flash1: %s (ID=%d, Size=%dMB)\n",
                 g_flash_status.flash1.ready ? "Ready" : "Not Ready",
                 g_flash_status.flash1.device_id,
                 g_flash_status.flash1.total_size / (1024*1024));
    
    return BG_FLASH_OK;
}

static void bg_flash_deinit(void)
{
    if (!g_initialized) {
        return;
    }
    
    flash_lock(1000);
    
    FlashDevices_DeInit();
    
    g_initialized = false;
    memset(&g_flash_status, 0, sizeof(g_flash_status));
    
    flash_unlock();
    
    if (g_flash_mutex) {
        vSemaphoreDelete(g_flash_mutex);
        g_flash_mutex = NULL;
    }
    
    FLASHMGR_LOG("DeInitialized\n");
}

/*===========================================================================
 * 系统分区操作
 *===========================================================================*/

static int32_t bg_flash_read_system(uint32_t offset, uint8_t *buffer, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!buffer || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_system_range(offset, size)) {
        FLASHMGR_LOG("System read out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(1000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_SystemRead(offset, buffer, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_READ;
}

static int32_t bg_flash_write_system(uint32_t offset, const uint8_t *data, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!data || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_system_range(offset, size)) {
        FLASHMGR_LOG("System write out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(2000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_SystemWrite(offset, data, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_WRITE;
}

static int32_t bg_flash_erase_system_sector(uint32_t offset)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!is_valid_system_range(offset, BG_FLASH_SECTOR_SIZE)) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(5000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_SystemEraseSector(offset);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_ERASE;
}

/*===========================================================================
 * Looper分区操作
 *===========================================================================*/

static int32_t bg_flash_read_looper(uint32_t offset, uint8_t *buffer, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!buffer || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_looper_range(offset, size)) {
        FLASHMGR_LOG("Looper read out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(1000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_LooperRead(offset, buffer, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_READ;
}

static int32_t bg_flash_write_looper(uint32_t offset, const uint8_t *data, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!data || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_looper_range(offset, size)) {
        FLASHMGR_LOG("Looper write out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(2000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_LooperWrite(offset, data, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_WRITE;
}

static int32_t bg_flash_erase_looper_sector(uint32_t offset)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!is_valid_looper_range(offset, BG_FLASH_SECTOR_SIZE)) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(5000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_LooperEraseSector(offset);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_ERASE;
}

static int32_t bg_flash_erase_looper_block(uint32_t offset)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!is_valid_looper_range(offset, BG_FLASH_BLOCK_SIZE)) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(10000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_LooperEraseBlock(offset);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash0.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_ERASE;
}

static int32_t bg_flash_erase_looper_all(void)
{
    FlashDevice_t *dev;
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    dev = FlashDevices_GetSystemFlash();
    if (!dev || !dev->initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!flash_lock(30000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    FLASHMGR_LOG("Erasing entire Looper partition (7MB)...\n");
    
    /* 按块擦除效率更高 */
    uint32_t offset = 0;
    while (offset < BG_FLASH_PARTITION_LOOPER_SIZE) {
        ret = FlashPartition_LooperEraseBlock(offset);
        if (ret != FLASH_OK) {
            FLASHMGR_LOG("Erase failed at offset 0x%X\n", offset);
            g_flash_status.flash0.error_count++;
            flash_unlock();
            return BG_FLASH_ERROR_ERASE;
        }
        offset += BG_FLASH_BLOCK_SIZE;
        
        /* 每擦除1MB打印进度 */
        if ((offset % (1024*1024)) == 0) {
            FLASHMGR_LOG("  Erased %d MB...\n", offset / (1024*1024));
        }
    }
    
    flash_unlock();
    
    FLASHMGR_LOG("Looper partition erased\n");
    return BG_FLASH_OK;
}

/*===========================================================================
 * 存储分区操作
 *===========================================================================*/

static int32_t bg_flash_read_storage(uint32_t offset, uint8_t *buffer, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!buffer || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_storage_range(offset, size)) {
        FLASHMGR_LOG("Storage read out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(1000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_StorageRead(offset, buffer, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash1.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_READ;
}

static int32_t bg_flash_write_storage(uint32_t offset, const uint8_t *data, uint32_t size)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!data || size == 0) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!is_valid_storage_range(offset, size)) {
        FLASHMGR_LOG("Storage write out of range: offset=0x%X, size=%d\n", offset, size);
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(2000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_StorageWrite(offset, data, size);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash1.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_WRITE;
}

static int32_t bg_flash_erase_storage_sector(uint32_t offset)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!is_valid_storage_range(offset, BG_FLASH_SECTOR_SIZE)) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(5000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_StorageEraseSector(offset);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash1.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_ERASE;
}

static int32_t bg_flash_erase_storage_block(uint32_t offset)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!is_valid_storage_range(offset, BG_FLASH_BLOCK_SIZE)) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(10000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashPartition_StorageEraseBlock(offset);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash1.error_count++;
    }
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR_ERASE;
}

static int32_t bg_flash_erase_storage_all(void)
{
    FlashDevice_t *dev;
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    dev = FlashDevices_GetStorageFlash();
    if (!dev || !dev->initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!flash_lock(30000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    FLASHMGR_LOG("Erasing entire Storage partition (8MB)...\n");
    
    /* 使用全片擦除命令更快 */
    ret = FlashDev_EraseChip(dev);
    
    if (ret != FLASH_OK) {
        g_flash_status.flash1.error_count++;
        flash_unlock();
        return BG_FLASH_ERROR_ERASE;
    }
    
    flash_unlock();
    
    FLASHMGR_LOG("Storage partition erased\n");
    return BG_FLASH_OK;
}

/*===========================================================================
 * 状态查询
 *===========================================================================*/

static int32_t bg_flash_get_status(BG_FlashMgrStatus_t *status)
{
    if (!status) {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (!flash_lock(100)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    /* 更新设备状态 */
    FlashDevice_t *dev0 = FlashDevices_GetSystemFlash();
    FlashDevice_t *dev1 = FlashDevices_GetStorageFlash();
    
    if (dev0) {
        update_device_status(dev0, &g_flash_status.flash0);
    }
    if (dev1) {
        update_device_status(dev1, &g_flash_status.flash1);
    }
    
    memcpy(status, &g_flash_status, sizeof(BG_FlashMgrStatus_t));
    
    flash_unlock();
    
    return BG_FLASH_OK;
}

static bool bg_flash_is_ready(void)
{
    return g_initialized && g_flash_status.flash0.ready;
}

static uint32_t bg_flash_get_looper_free_space(void)
{
    /* 简化实现: 返回总容量 */
    /* TODO: 实现真实的空间统计 */
    return BG_FLASH_PARTITION_LOOPER_SIZE;
}

static uint32_t bg_flash_get_storage_free_space(void)
{
    /* 简化实现: 返回总容量 */
    /* TODO: 实现真实的空间统计 */
    return BG_FLASH_PARTITION_STORAGE_SIZE;
}

/*===========================================================================
 * 测试与调试
 *===========================================================================*/

static int32_t bg_flash_test_device(uint8_t device_id)
{
    FlashStatus_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    if (!flash_lock(10000)) {
        return BG_FLASH_ERROR_TIMEOUT;
    }
    
    ret = FlashBus_TestDevice(device_id);
    
    flash_unlock();
    
    return (ret == FLASH_OK) ? BG_FLASH_OK : BG_FLASH_ERROR;
}

static void bg_flash_print_info(void)
{
    BG_FlashMgrStatus_t status;
    
    DBG("\n========== BG_FlashMgr Info ==========\n");
    DBG("Status: %s\n", g_initialized ? "Initialized" : "Not Initialized");
    
    if (bg_flash_get_status(&status) == BG_FLASH_OK) {
        DBG("\nFlash #0 (System + Looper):\n");
        DBG("  Status: %s\n", status.flash0.ready ? "Ready" : "Not Ready");
        DBG("  Device ID: %d\n", status.flash0.device_id);
        DBG("  Total Size: %d MB\n", status.flash0.total_size / (1024*1024));
        DBG("  Error Count: %d\n", status.flash0.error_count);
        DBG("  Partitions:\n");
        DBG("    - System: 1 MB\n");
        DBG("    - Looper: 7 MB\n");
        
        DBG("\nFlash #1 (Storage):\n");
        DBG("  Status: %s\n", status.flash1.ready ? "Ready" : "Not Ready");
        DBG("  Device ID: %d\n", status.flash1.device_id);
        DBG("  Total Size: %d MB\n", status.flash1.total_size / (1024*1024));
        DBG("  Error Count: %d\n", status.flash1.error_count);
        DBG("  Partitions:\n");
        DBG("    - Storage: 8 MB (Full chip)\n");
    }
    
    DBG("\nFree Space:\n");
    DBG("  Looper: %d KB\n", bg_flash_get_looper_free_space() / 1024);
    DBG("  Storage: %d KB\n", bg_flash_get_storage_free_space() / 1024);
    
    DBG("======================================\n\n");
}

static int32_t bg_flash_format(uint8_t device_id)
{
    int32_t ret;
    
    if (!g_initialized) {
        return BG_FLASH_ERROR_NOT_INIT;
    }
    
    FLASHMGR_LOG("Formatting device %d...\n", device_id);
    
    if (device_id == 0) {
        /* 格式化Looper分区 (不动系统分区) */
        ret = bg_flash_erase_looper_all();
    } else if (device_id == 1) {
        /* 格式化存储分区 */
        ret = bg_flash_erase_storage_all();
    } else {
        return BG_FLASH_ERROR_PARAM;
    }
    
    if (ret == BG_FLASH_OK) {
        FLASHMGR_LOG("Format completed\n");
    } else {
        FLASHMGR_LOG("Format failed: %d\n", ret);
    }
    
    return ret;
}

/*===========================================================================
 * 全局实例
 *===========================================================================*/

BG_FlashMgr_t BG_FlashMgr = {
    /* 初始化与反初始化 */
    .Init               = bg_flash_init,
    .DeInit             = bg_flash_deinit,
    
    /* 系统分区操作 */
    .ReadSystem         = bg_flash_read_system,
    .WriteSystem        = bg_flash_write_system,
    .EraseSystemSector  = bg_flash_erase_system_sector,
    
    /* Looper分区操作 */
    .ReadLooper         = bg_flash_read_looper,
    .WriteLooper        = bg_flash_write_looper,
    .EraseLooperSector  = bg_flash_erase_looper_sector,
    .EraseLooperBlock   = bg_flash_erase_looper_block,
    .EraseLooperAll     = bg_flash_erase_looper_all,
    
    /* 存储分区操作 */
    .ReadStorage        = bg_flash_read_storage,
    .WriteStorage       = bg_flash_write_storage,
    .EraseStorageSector = bg_flash_erase_storage_sector,
    .EraseStorageBlock  = bg_flash_erase_storage_block,
    .EraseStorageAll    = bg_flash_erase_storage_all,
    
    /* 状态查询 */
    .GetStatus          = bg_flash_get_status,
    .IsReady            = bg_flash_is_ready,
    .GetLooperFreeSpace = bg_flash_get_looper_free_space,
    .GetStorageFreeSpace= bg_flash_get_storage_free_space,
    
    /* 测试与调试 */
    .TestDevice         = bg_flash_test_device,
    .PrintInfo          = bg_flash_print_info,
    .Format             = bg_flash_format,
};
