/**
 * flash_nor_w25qxx.h - W25Qxx系列NOR Flash驱动
 * 
 * 支持型号: W25Q32, W25Q64, W25Q128, W25Q256 等
 */

#ifndef __FLASH_NOR_W25QXX_H__
#define __FLASH_NOR_W25QXX_H__

#include "flash_bus.h"

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * W25Qxx 命令集
 *===========================================================================*/

/* 基本命令 */
#define W25QXX_CMD_WRITE_ENABLE      0x06
#define W25QXX_CMD_WRITE_DISABLE     0x04
#define W25QXX_CMD_READ_STATUS_REG1  0x05
#define W25QXX_CMD_READ_STATUS_REG2  0x35
#define W25QXX_CMD_WRITE_STATUS_REG  0x01
#define W25QXX_CMD_READ_DATA         0x03
#define W25QXX_CMD_FAST_READ         0x0B
#define W25QXX_CMD_PAGE_PROGRAM      0x02
#define W25QXX_CMD_SECTOR_ERASE      0x20    /* 4KB */
#define W25QXX_CMD_BLOCK_ERASE_32K   0x52    /* 32KB */
#define W25QXX_CMD_BLOCK_ERASE_64K   0xD8    /* 64KB */
#define W25QXX_CMD_CHIP_ERASE        0xC7
#define W25QXX_CMD_POWER_DOWN        0xB9
#define W25QXX_CMD_RELEASE_PD        0xAB
#define W25QXX_CMD_READ_JEDEC_ID     0x9F
#define W25QXX_CMD_READ_UNIQUE_ID    0x4B

/* 状态寄存器位 */
#define W25QXX_SR1_BUSY              0x01
#define W25QXX_SR1_WEL               0x02
#define W25QXX_SR1_BP0               0x04
#define W25QXX_SR1_BP1               0x08
#define W25QXX_SR1_BP2               0x10
#define W25QXX_SR1_TB                0x20
#define W25QXX_SR1_SEC               0x40
#define W25QXX_SR1_SRP               0x80

/* 厂商ID */
#define W25QXX_MFG_WINBOND           0xEF

/* 设备类型 */
#define W25QXX_DEV_Q32               0x16    /* W25Q32: 4MB */
#define W25QXX_DEV_Q64               0x17    /* W25Q64: 8MB */
#define W25QXX_DEV_Q128              0x18    /* W25Q128: 16MB */
#define W25QXX_DEV_Q256              0x19    /* W25Q256: 32MB */

/* 规格参数 */
#define W25QXX_PAGE_SIZE             256
#define W25QXX_SECTOR_SIZE           4096
#define W25QXX_BLOCK_SIZE_32K        (32 * 1024)
#define W25QXX_BLOCK_SIZE_64K        (64 * 1024)

/* 超时设置 (ms) */
#define W25QXX_TIMEOUT_WRITE_PAGE    5
#define W25QXX_TIMEOUT_ERASE_SECTOR  500   /* W25Q64 sector erase max 400ms, +25% margin */

#define W25QXX_TIMEOUT_ERASE_BLOCK   400
#define W25QXX_TIMEOUT_ERASE_CHIP    100000

/*===========================================================================
 * W25Qxx 驱动接口
 *===========================================================================*/

/**
 * @brief 创建W25Qxx设备实例
 * @param name      设备名称
 * @param cs_select CS选中函数
 * @param cs_deselect CS取消选中函数
 * @param cs_init   CS初始化函数 (可选)
 * @return 设备指针，失败返回NULL
 */
FlashDevice_t* W25Qxx_Create(const char *name,
                             void (*cs_select)(void),
                             void (*cs_deselect)(void),
                             void (*cs_init)(void));

/**
 * @brief 销毁W25Qxx设备实例
 * @param dev 设备指针
 */
void W25Qxx_Destroy(FlashDevice_t *dev);

/**
 * @brief 获取W25Qxx驱动操作表
 * @return 操作表指针
 */
const FlashOps_t* W25Qxx_GetOps(void);

/**
 * @brief 发送 64KB 块擦除命令后立即返回（非阻塞）
 *
 * 只发送擦除命令，不等待完成；调用后需通过 W25Qxx_IsBusy()
 * 轮询直到返回 0，才可进行下一次写/擦操作。
 *
 * @param dev   设备指针
 * @param addr  块内任意地址（自动对齐到 64KB 边界）
 * @return FLASH_OK / FLASH_ERR_*
 */
FlashStatus_t W25Qxx_EraseBlockStart(FlashDevice_t *dev, uint32_t addr);

/**
 * @brief 发送全片擦除命令后立即返回（非阻塞）
 *
 * 调用后需轮询 W25Qxx_IsBusy() 确认擦除结束，
 * 擦除期间不得对 Flash 进行写/擦除操作。
 *
 * @param dev  设备指针
 * @return FLASH_OK / FLASH_ERR_*
 */
FlashStatus_t W25Qxx_EraseChipStart(FlashDevice_t *dev);

/**
 * @brief 非阻塞忙状态查询
 *
 * 读取 SR1 BUSY 位，不产生任何等待延迟。
 *
 * @param dev  设备指针
 * @return 1 = 仍在擦除；0 = 擦除完成（或设备无效）
 */
uint8_t W25Qxx_IsBusy(FlashDevice_t *dev);

/*===========================================================================
 * IOCTL命令
 *===========================================================================*/

#define W25QXX_IOCTL_POWER_DOWN      0x01
#define W25QXX_IOCTL_RELEASE_PD      0x02
#define W25QXX_IOCTL_GET_UNIQUE_ID   0x03
#define W25QXX_IOCTL_WRITE_PROTECT   0x04
#define W25QXX_IOCTL_WRITE_UNPROTECT 0x05

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_NOR_W25QXX_H__ */
