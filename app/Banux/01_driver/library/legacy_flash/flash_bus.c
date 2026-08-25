/**
 * flash_bus.c - Flash鎬荤嚎绠＄悊鍣ㄥ疄鐜�
 */

#include "flash_bus.h"
#include "debug.h"
#include <string.h>
#include <stdio.h>

/*===========================================================================
 * 鎬荤嚎鍗曚緥
 *===========================================================================*/

static FlashBus_t g_flash_bus = {0};

/*===========================================================================
 * 鎬荤嚎绠＄悊
 *===========================================================================*/

FlashStatus_t FlashBus_Init(void)
{
    if (g_flash_bus.initialized)
    {
        return FLASH_OK;
    }

    memset(&g_flash_bus, 0, sizeof(FlashBus_t));
    g_flash_bus.initialized = true;

    DBG("[FlashBus] Initialized\n");
    return FLASH_OK;
}

void FlashBus_DeInit(void)
{
    FlashDevice_t *dev = g_flash_bus.head;

    while (dev)
    {
        if (dev->initialized && dev->ops && dev->ops->deinit)
        {
            dev->ops->deinit(dev);
        }
        dev = dev->next;
    }

    memset(&g_flash_bus, 0, sizeof(FlashBus_t));
    DBG("[FlashBus] DeInitialized\n");
}

FlashBus_t *FlashBus_GetInstance(void)
{
    return &g_flash_bus;
}

FlashStatus_t FlashBus_Register(FlashDevice_t *dev)
{
    if (!dev)
    {
        return FLASH_ERR_PARAM;
    }

    if (!g_flash_bus.initialized)
    {
        FlashBus_Init();
    }

    if (g_flash_bus.device_count >= FLASH_BUS_MAX_DEVICES)
    {
        DBG("[FlashBus] Bus full, cannot register %s\n", dev->name);
        return FLASH_ERR_FULL;
    }

    if (dev->registered)
    {
        DBG("[FlashBus] Device %s already registered\n", dev->name);
        return FLASH_OK;
    }

    /* 鍒嗛厤ID */
    dev->id = g_flash_bus.device_count;
    dev->registered = true;

    /* 娣诲姞鍒版暟缁�*/
    g_flash_bus.devices[dev->id] = dev;

    /* 娣诲姞鍒伴摼琛�*/
    dev->next = g_flash_bus.head;
    g_flash_bus.head = dev;

    g_flash_bus.device_count++;

    DBG("[FlashBus] Registered: %s (id=%d, type=%s)\n",
        dev->name, dev->id,
        dev->type == FLASH_TYPE_NOR    ? "NOR"   :
        dev->type == FLASH_TYPE_NAND   ? "NAND"  :
        dev->type == FLASH_TYPE_PSRAM  ? "PSRAM" :
        dev->type == FLASH_TYPE_SDCARD ? "SDCARD": "UNKNOWN");

    return FLASH_OK;
}

FlashStatus_t FlashBus_Unregister(FlashDevice_t *dev)
{
    FlashDevice_t *curr, *prev = NULL;

    if (!dev || !dev->registered)
    {
        return FLASH_ERR_PARAM;
    }

    /* 浠庨摼琛ㄧЩ闄�*/
    curr = g_flash_bus.head;
    while (curr)
    {
        if (curr == dev)
        {
            if (prev)
            {
                prev->next = curr->next;
            }
            else
            {
                g_flash_bus.head = curr->next;
            }
            break;
        }
        prev = curr;
        curr = curr->next;
    }

    /* 浠庢暟缁勭Щ闄�*/
    if (dev->id < FLASH_BUS_MAX_DEVICES)
    {
        g_flash_bus.devices[dev->id] = NULL;
    }

    dev->registered = false;
    g_flash_bus.device_count--;

    DBG("[FlashBus] Unregistered: %s\n", dev->name);
    return FLASH_OK;
}

FlashDevice_t *FlashBus_GetDeviceById(uint8_t id)
{
    if (id >= FLASH_BUS_MAX_DEVICES)
    {
        return NULL;
    }
    return g_flash_bus.devices[id];
}

FlashDevice_t *FlashBus_GetDeviceByName(const char *name)
{
    FlashDevice_t *dev = g_flash_bus.head;

    if (!name)
        return NULL;

    while (dev)
    {
        if (strcmp(dev->name, name) == 0)
        {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

uint8_t FlashBus_GetDeviceCount(void)
{
    return g_flash_bus.device_count;
}

void FlashBus_ForEach(void (*callback)(FlashDevice_t *dev, void *user_data), void *user_data)
{
    FlashDevice_t *dev = g_flash_bus.head;

    if (!callback)
        return;

    while (dev)
    {
        callback(dev, user_data);
        dev = dev->next;
    }
}

/*===========================================================================
 * 璁惧鎿嶄綔渚挎嵎API
 *===========================================================================*/

FlashStatus_t FlashDev_Init(FlashDevice_t *dev)
{
    FlashStatus_t ret;

    if (!dev || !dev->ops || !dev->ops->init)
    {
        return FLASH_ERR_PARAM;
    }

    /* 初始化CS引脚 */
    if (dev->cs.init)
    {
        dev->cs.init();
        dev->cs.deselect();
    }

    ret = dev->ops->init(dev);
    if (ret == FLASH_OK)
    {
        dev->initialized = true;   /* 初始化成功才置位,所有读写保护依赖此标志 */
    }
    return ret;
}

FlashStatus_t FlashDev_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!dev || !dev->ops || !dev->ops->read)
    {
        return FLASH_ERR_PARAM;
    }
    if (!dev->initialized)
    {
        return FLASH_ERR_NOT_INIT;
    }
    return dev->ops->read(dev, addr, buf, len);
}

FlashStatus_t FlashDev_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!dev || !dev->ops || !dev->ops->write)
    {
        return FLASH_ERR_PARAM;
    }
    if (!dev->initialized)
    {
        return FLASH_ERR_NOT_INIT;
    }
    return dev->ops->write(dev, addr, buf, len);
}

FlashStatus_t FlashDev_EraseSector(FlashDevice_t *dev, uint32_t addr)
{
    if (!dev || !dev->ops || !dev->ops->erase_sector)
    {
        return FLASH_ERR_PARAM;
    }
    if (!dev->initialized)
    {
        return FLASH_ERR_NOT_INIT;
    }
    return dev->ops->erase_sector(dev, addr);
}

FlashStatus_t FlashDev_EraseBlock(FlashDevice_t *dev, uint32_t addr)
{
    if (!dev || !dev->ops || !dev->ops->erase_block)
    {
        return FLASH_ERR_PARAM;
    }
    if (!dev->initialized)
    {
        return FLASH_ERR_NOT_INIT;
    }
    return dev->ops->erase_block(dev, addr);
}

FlashStatus_t FlashDev_EraseChip(FlashDevice_t *dev)
{
    if (!dev || !dev->ops || !dev->ops->erase_chip)
    {
        return FLASH_ERR_PARAM;
    }
    if (!dev->initialized)
    {
        return FLASH_ERR_NOT_INIT;
    }
    return dev->ops->erase_chip(dev);
}

void FlashDev_PrintInfo(FlashDevice_t *dev)
{
    if (!dev)
        return;

    DBG("  [%d] %s\n", dev->id, dev->name);
    DBG("      Type: %s\n",
        dev->type == FLASH_TYPE_NOR    ? "NOR"   :
        dev->type == FLASH_TYPE_NAND   ? "NAND"  :
        dev->type == FLASH_TYPE_PSRAM  ? "PSRAM" :
        dev->type == FLASH_TYPE_SDCARD ? "SDCARD": "UNKNOWN");
    DBG("      Status: %s\n", dev->initialized ? "Ready" : "Not Init");

    if (dev->initialized)
    {
        DBG("      ID: Mfg=0x%02X Type=0x%02X Dev=0x%02X\n",
            dev->info.mfg_id, dev->info.mem_type, dev->info.dev_id);
        DBG("      Size: %d KB (%d MB)\n",
            dev->info.total_size / 1024,
            dev->info.total_size / (1024 * 1024));
        DBG("      Page: %d B, Sector: %d KB, Block: %d KB\n",
            dev->info.page_size,
            dev->info.sector_size / 1024,
            dev->info.block_size / 1024);
    }
}

/*===========================================================================
 * 璋冭瘯鎺ュ彛
 *===========================================================================*/

void FlashBus_PrintInfo(void)
{
    DBG("\n======== Flash Bus Info ========\n");
    DBG("Devices: %d/%d\n", g_flash_bus.device_count, FLASH_BUS_MAX_DEVICES);
    DBG("--------------------------------\n");

    FlashDevice_t *dev = g_flash_bus.head;
    while (dev)
    {
        FlashDev_PrintInfo(dev);
        dev = dev->next;
    }

    DBG("================================\n\n");
}

FlashStatus_t FlashBus_TestDevice(uint8_t id)
{
    FlashDevice_t *dev = FlashBus_GetDeviceById(id);
    uint8_t write_buf[256];
    uint8_t read_buf[256];
    FlashStatus_t ret;
    uint32_t test_addr = 0;
    int i;

    if (!dev)
    {
        DBG("[FlashBus] Device %d not found\n", id);
        return FLASH_ERR_NOT_FOUND;
    }

    if (!dev->initialized)
    {
        DBG("[FlashBus] Device %d not initialized\n", id);
        return FLASH_ERR_NOT_INIT;
    }

    DBG("\n=== Testing %s (id=%d) ===\n", dev->name, id);

    /* 鍑嗗娴嬭瘯鏁版嵁 */
    for (i = 0; i < 256; i++)
    {
        write_buf[i] = i;
    }

    /* 鎿﹂櫎 */
    DBG("Erasing sector at 0x%06X...\n", test_addr);
    ret = FlashDev_EraseSector(dev, test_addr);
    if (ret != FLASH_OK)
    {
        DBG("Erase failed! ret=%d\n", ret);
        return ret;
    }

    /* 楠岃瘉鎿﹂櫎 */
    ret = FlashDev_Read(dev, test_addr, read_buf, 256);
    if (ret != FLASH_OK)
    {
        DBG("Read failed!\n");
        return ret;
    }

    for (i = 0; i < 256; i++)
    {
        if (read_buf[i] != 0xFF)
        {
            DBG("Erase verify failed at %d: 0x%02X\n", i, read_buf[i]);
            return FLASH_ERR_ERASE;
        }
    }
    DBG("Erase verified OK\n");

    /* 鍐欏叆 */
    DBG("Writing test data...\n");
    ret = FlashDev_Write(dev, test_addr, write_buf, 256);
    if (ret != FLASH_OK)
    {
        DBG("Write failed!\n");
        return ret;
    }

    /* 璇诲彇楠岃瘉 */
    memset(read_buf, 0, 256);
    ret = FlashDev_Read(dev, test_addr, read_buf, 256);
    if (ret != FLASH_OK)
    {
        DBG("Read failed!\n");
        return ret;
    }

    for (i = 0; i < 256; i++)
    {
        if (read_buf[i] != write_buf[i])
        {
            DBG("Verify failed at %d: wrote 0x%02X, read 0x%02X\n",
                i, write_buf[i], read_buf[i]);
            return FLASH_ERR_VERIFY;
        }
    }

    DBG("Write/Read verified OK\n");
    DBG("=== Test PASSED ===\n\n");

    return FLASH_OK;
}

/*===========================================================================
 * Shell鍛戒护
 *===========================================================================*/

int FlashBus_ShellCmd(int argc, char *argv[])
{

    uint32_t i;
    if (argc < 1)
    {
        DBG("Usage: flash <cmd> [args]\n");
        DBG("Commands:\n");
        DBG("  list          - List all devices\n");
        DBG("  info <id>     - Show device info\n");
        DBG("  init <id>     - Initialize device\n");
        DBG("  test <id>     - Test device read/write\n");
        DBG("  read <id> <addr> [len]  - Read data\n");
        DBG("  erase <id> <addr>       - Erase sector\n");
        DBG("  eraseall <id>           - Erase entire chip\n");
        return 0;
    }

    const char *cmd = argv[0];

    /* list - 鍒楀嚭鎵�湁璁惧 */
    if (strcmp(cmd, "list") == 0)
    {
        FlashBus_PrintInfo();
        return 0;
    }

    /* info <id> - 鏄剧ず璁惧淇℃伅 */
    if (strcmp(cmd, "info") == 0)
    {
        if (argc < 2)
        {
            DBG("Usage: flash info <id>\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);
        FlashDevice_t *dev = FlashBus_GetDeviceById(id);
        if (dev)
        {
            FlashDev_PrintInfo(dev);
        }
        else
        {
            DBG("Device %d not found\n", id);
        }
        return 0;
    }

    /* init <id> - 鍒濆鍖栬澶�*/
    if (strcmp(cmd, "init") == 0)
    {
        if (argc < 2)
        {
            DBG("Usage: flash init <id>\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);
        FlashDevice_t *dev = FlashBus_GetDeviceById(id);
        if (dev)
        {
            FlashStatus_t ret = FlashDev_Init(dev);
            DBG("Init %s: %s\n", dev->name, ret == FLASH_OK ? "OK" : "Failed");
        }
        else
        {
            DBG("Device %d not found\n", id);
        }
        return 0;
    }

    /* test <id> - 娴嬭瘯璁惧 */
    if (strcmp(cmd, "test") == 0)
    {
        if (argc < 2)
        {
            DBG("Usage: flash test <id>\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);
        FlashBus_TestDevice(id);
        return 0;
    }

    /* read <id> <addr> [len] - 璇诲彇鏁版嵁 */
    if (strcmp(cmd, "read") == 0)
    {
        if (argc < 3)
        {
            DBG("Usage: flash read <id> <addr> [len]\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);
        uint32_t addr = strtoul(argv[2], NULL, 0);
        uint32_t len = (argc >= 4) ? atoi(argv[3]) : 64;

        if (len > 256)
            len = 256;

        FlashDevice_t *dev = FlashBus_GetDeviceById(id);
        if (!dev)
        {
            DBG("Device %d not found\n", id);
            return -1;
        }

        uint8_t buf[256];
        FlashStatus_t ret = FlashDev_Read(dev, addr, buf, len);
        if (ret != FLASH_OK)
        {
            DBG("Read failed: %d\n", ret);
            return -1;
        }

        DBG("Read %d bytes from 0x%06X:\n", len, addr);
        for (i = 0; i < len; i++)
        {
            if ((i % 16) == 0)
                DBG("%06X: ", addr + i);
            DBG("%02X ", buf[i]);
            if ((i % 16) == 15)
                DBG("\n");
        }
        if ((len % 16) != 0)
            DBG("\n");

        return 0;
    }

    /* erase <id> <addr> - 鎿﹂櫎鎵囧尯 */
    if (strcmp(cmd, "erase") == 0)
    {
        if (argc < 3)
        {
            DBG("Usage: flash erase <id> <addr>\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);
        uint32_t addr = strtoul(argv[2], NULL, 0);

        FlashDevice_t *dev = FlashBus_GetDeviceById(id);
        if (!dev)
        {
            DBG("Device %d not found\n", id);
            return -1;
        }

        DBG("Erasing sector at 0x%06X...\n", addr);
        FlashStatus_t ret = FlashDev_EraseSector(dev, addr);
        DBG("Erase %s\n", ret == FLASH_OK ? "OK" : "Failed");

        return 0;
    }

    /* eraseall <id> - 鍏ㄧ墖鎿﹂櫎 */
    if (strcmp(cmd, "eraseall") == 0)
    {
        if (argc < 2)
        {
            DBG("Usage: flash eraseall <id>\n");
            return -1;
        }
        uint8_t id = atoi(argv[1]);

        FlashDevice_t *dev = FlashBus_GetDeviceById(id);
        if (!dev)
        {
            DBG("Device %d not found\n", id);
            return -1;
        }

        DBG("WARNING: Erasing entire chip %s...\n", dev->name);
        FlashStatus_t ret = FlashDev_EraseChip(dev);
        DBG("Chip erase %s\n", ret == FLASH_OK ? "OK" : "Failed");

        return 0;
    }

    DBG("Unknown command: %s\n", cmd);
    return -1;
}
