/**
 * flash_api.h - Flash子系统统一API
 * 
 * 使用方法:
 *   #include "flash_api.h"
 * 
 * 初始化:
 *   FlashDevices_Init();
 * 
 * 分区操作:
 *   FlashPartition_LooperRead(offset, buf, len);
 *   FlashPartition_LooperWrite(offset, buf, len);
 *   FlashPartition_LooperEraseSector(offset);
 * 
 * 底层设备操作:
 *   FlashDevice_t *dev = FlashBus_GetDeviceById(0);
 *   FlashDev_Read(dev, addr, buf, len);
 */

#ifndef __FLASH_API_H__
#define __FLASH_API_H__

#include "flash_bus.h"
#include "flash_nor_w25qxx.h"
#include "flash_devices.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * 快速参考
 *===========================================================================*/

/*
 * 架构层次:
 * 
 *   +-------------------+
 *   | 应用层            |  FlashPartition_xxx() - 分区读写
 *   +-------------------+
 *   | 设备层            |  flash_devices.c - 设备注册
 *   +-------------------+
 *   | 总线层            |  flash_bus.c - 设备管理
 *   +-------------------+
 *   | 驱动层            |  flash_nor_w25qxx.c - 硬件驱动
 *   +-------------------+
 *   | 硬件              |  SPI Flash (W25Q64 x 2)
 *   +-------------------+
 *
 * 分区布局:
 * 
 *   Flash #0 (CS=GPIOA21):
 *   +----------------------------+
 *   | System Partition (1MB)     | 0x000000 - 0x0FFFFF
 *   +----------------------------+
 *   | Looper Partition (7MB)     | 0x100000 - 0x7FFFFF
 *   +----------------------------+
 * 
 *   Flash #1 (CS=GPIOA23):
 *   +----------------------------+
 *   | Storage Partition (8MB)    | 0x000000 - 0x7FFFFF
 *   +----------------------------+
 */

/*===========================================================================
 * 常用API一览
 *===========================================================================*/

/*
 * 初始化 (在main.c中调用一次)
 * --------------------------------
 * FlashDevices_Init()      - 初始化所有Flash设备
 * FlashDevices_DeInit()    - 反初始化
 *
 * 分区操作 (推荐使用)
 * --------------------------------
 * FlashPartition_SystemRead/Write/EraseSector()    - 系统分区
 * FlashPartition_LooperRead/Write/EraseSector/EraseBlock() - Looper分区
 * FlashPartition_StorageRead/Write/EraseSector/EraseBlock() - 存储分区
 *
 * 设备操作 (底层)
 * --------------------------------
 * FlashBus_GetDeviceById(id)   - 获取设备
 * FlashDev_Read/Write()        - 读写数据
 * FlashDev_EraseSector/Block() - 擦除
 *
 * 调试
 * --------------------------------
 * FlashBus_PrintInfo()     - 打印所有设备信息
 * FlashBus_TestDevice(id)  - 测试指定设备
 *
 * Shell命令
 * --------------------------------
 * flash list              - 列出所有设备
 * flash info <id>         - 显示设备详情
 * flash init <id>         - 初始化设备
 * flash test <id>         - 测试读写
 * flash read <id> <addr>  - 读取数据
 * flash erase <id> <addr> - 擦除扇区
 * flash eraseall <id>     - 全片擦除
 */

/*===========================================================================
 * 使用示例
 *===========================================================================*/

/*
 * 例1: 简单分区读写
 * 
 *   uint8_t buf[256];
 *   
 *   // 擦除Looper分区第一个扇区
 *   FlashPartition_LooperEraseSector(0);
 *   
 *   // 写入数据
 *   memset(buf, 0xAA, 256);
 *   FlashPartition_LooperWrite(0, buf, 256);
 *   
 *   // 读取验证
 *   FlashPartition_LooperRead(0, buf, 256);
 *
 * 例2: 底层设备操作
 * 
 *   FlashDevice_t *dev = FlashBus_GetDeviceById(0);
 *   if (dev && dev->initialized) {
 *       FlashDev_Read(dev, 0x100000, buf, 256);
 *   }
 *
 * 例3: 遍历所有设备
 * 
 *   void print_dev(FlashDevice_t *dev, void *arg) {
 *       DBG("Device: %s\n", dev->name);
 *   }
 *   FlashBus_ForEach(print_dev, NULL);
 */

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_API_H__ */
