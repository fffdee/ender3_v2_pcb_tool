/**
 * flash_driver.h - Flash底层驱动抽象层
 * 
 * 本文件定义Flash驱动的抽象接口，支持：
 * - 多颗同型号Flash通过不同CS引脚控制
 * - NOR Flash (W25Q64等) 和 NAND Flash (W25N02等)
 * - 统一的驱动接口，便于上层管理
 */

#ifndef __FLASH_DRIVER_H__
#define __FLASH_DRIVER_H__

#include <stdint.h>
#include <stdbool.h>
#include "gpio.h"

/*===========================================================================
 * 常量定义
 *===========================================================================*/

/* Flash类型定义 */
typedef enum {
    FLASH_TYPE_NOR = 0,     /* NOR Flash (W25Qxx系列) */
    FLASH_TYPE_NAND,        /* NAND Flash (W25Nxx系列) */
    FLASH_TYPE_MAX
} FlashType_t;

/* Flash型号定义 */
typedef enum {
    FLASH_MODEL_UNKNOWN = 0,
    /* NOR Flash */
    FLASH_MODEL_W25Q32,     /* 4MB */
    FLASH_MODEL_W25Q64,     /* 8MB */
    FLASH_MODEL_W25Q128,    /* 16MB */
    /* NAND Flash */
    FLASH_MODEL_W25N01,     /* 128MB */
    FLASH_MODEL_W25N02,     /* 256MB */
    FLASH_MODEL_MAX
} FlashModel_t;

/* 操作状态码 */
typedef enum {
    FLASH_OK = 0,
    FLASH_ERROR_BUSY,
    FLASH_ERROR_TIMEOUT,
    FLASH_ERROR_BAD_BLOCK,
    FLASH_ERROR_ECC,
    FLASH_ERROR_PROGRAM_FAIL,
    FLASH_ERROR_ERASE_FAIL,
    FLASH_ERROR_PARAM,
    FLASH_ERROR_NOT_INIT
} FlashStatus_t;

/* Flash信息结构 */
typedef struct {
    FlashType_t type;           /* Flash类型 */
    FlashModel_t model;         /* Flash型号 */
    uint8_t manufacturer_id;    /* 制造商ID */
    uint8_t memory_type;        /* 内存类型 */
    uint8_t device_id;          /* 设备ID */
    uint32_t total_size;        /* 总容量(字节) */
    uint32_t page_size;         /* 页大小(字节) */
    uint32_t sector_size;       /* 扇区大小(字节) */
    uint32_t block_size;        /* 块大小(字节) */
    uint32_t block_count;       /* 块数量 */
} FlashInfo_t;

/* CS引脚控制函数类型 */
typedef void (*FlashCsFunc_t)(bool enable);

/* Flash设备配置 */
typedef struct {
    FlashCsFunc_t cs_enable;    /* CS使能函数 */
    FlashCsFunc_t cs_disable;   /* CS禁用函数（可选，为NULL时使用enable(false)） */
    uint32_t gpio_port;         /* GPIO端口（用于初始化） */
    uint32_t gpio_pin;          /* GPIO引脚（用于初始化） */
} FlashCsConfig_t;

/*===========================================================================
 * Flash驱动结构体
 *===========================================================================*/

/* 前向声明 */
typedef struct FlashDriver FlashDriver_t;

/* Flash驱动操作接口 */
struct FlashDriver {
    /* 设备标识 */
    uint8_t id;                 /* 设备ID (0-based) */
    FlashType_t type;           /* Flash类型 */
    bool initialized;           /* 是否已初始化 */
    
    /* 设备信息 */
    FlashInfo_t info;           /* Flash信息 */
    
    /* CS控制 */
    FlashCsConfig_t cs_config;  /* CS配置 */
    
    /* 私有数据 */
    void *priv;                 /* 驱动私有数据 */
    
    /* 基本操作 */
    FlashStatus_t (*init)(FlashDriver_t *drv);
    FlashStatus_t (*deinit)(FlashDriver_t *drv);
    FlashStatus_t (*read_id)(FlashDriver_t *drv, uint8_t *mfg, uint8_t *type, uint8_t *dev);
    
    /* 读写操作 */
    FlashStatus_t (*read)(FlashDriver_t *drv, uint32_t addr, uint8_t *buf, uint32_t len);
    FlashStatus_t (*write)(FlashDriver_t *drv, uint32_t addr, const uint8_t *buf, uint32_t len);
    
    /* 擦除操作 */
    FlashStatus_t (*erase_sector)(FlashDriver_t *drv, uint32_t addr);
    FlashStatus_t (*erase_block)(FlashDriver_t *drv, uint32_t addr);
    FlashStatus_t (*erase_chip)(FlashDriver_t *drv);
    
    /* 状态操作 */
    FlashStatus_t (*get_status)(FlashDriver_t *drv, uint8_t *status);
    FlashStatus_t (*wait_ready)(FlashDriver_t *drv, uint32_t timeout_ms);
    
    /* 电源管理 */
    FlashStatus_t (*power_down)(FlashDriver_t *drv);
    FlashStatus_t (*power_up)(FlashDriver_t *drv);
};

/*===========================================================================
 * 预定义的CS引脚配置
 *===========================================================================*/

/* NOR Flash #0 (W25Q64) - GPIOA21 */
#define FLASH_NOR0_CS_INIT()    do { \
    GPIO_RegOneBitClear(GPIO_A_IE, GPIOA21); \
    GPIO_RegOneBitSet(GPIO_A_OE, GPIOA21); \
    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA21); \
} while(0)
#define FLASH_NOR0_CS_ENABLE()  GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA21)
#define FLASH_NOR0_CS_DISABLE() GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA21)

/* NOR Flash #1 (W25Q64) - GPIOA23 (示例，根据实际硬件修改) */
#define FLASH_NOR1_CS_INIT()    do { \
    GPIO_RegOneBitClear(GPIO_A_IE, GPIOA23); \
    GPIO_RegOneBitSet(GPIO_A_OE, GPIOA23); \
    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA23); \
} while(0)
#define FLASH_NOR1_CS_ENABLE()  GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA23)
#define FLASH_NOR1_CS_DISABLE() GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA23)

/* NAND Flash #0 (W25N02) - GPIOA22 */
#define FLASH_NAND0_CS_INIT()   do { \
    GPIO_RegOneBitClear(GPIO_A_IE, GPIOA22); \
    GPIO_RegOneBitSet(GPIO_A_OE, GPIOA22); \
    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22); \
} while(0)
#define FLASH_NAND0_CS_ENABLE()  GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA22)
#define FLASH_NAND0_CS_DISABLE() GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22)

/* WP引脚控制 */
#define FLASH_WP_INIT()    do { \
    GPIO_RegOneBitClear(GPIO_A_IE, GPIOA17); \
    GPIO_RegOneBitSet(GPIO_A_OE, GPIOA17); \
    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA17); \
} while(0)
#define FLASH_WP_ENABLE()   GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA17)
#define FLASH_WP_DISABLE()  GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA17)

/*===========================================================================
 * NOR Flash命令定义
 *===========================================================================*/
#define NOR_CMD_WRITE_ENABLE        0x06
#define NOR_CMD_WRITE_DISABLE       0x04
#define NOR_CMD_READ_STATUS         0x05
#define NOR_CMD_WRITE_STATUS        0x01
#define NOR_CMD_READ_DATA           0x03
#define NOR_CMD_FAST_READ           0x0B
#define NOR_CMD_PAGE_PROGRAM        0x02
#define NOR_CMD_SECTOR_ERASE_4K     0x20
#define NOR_CMD_BLOCK_ERASE_32K     0x52
#define NOR_CMD_BLOCK_ERASE_64K     0xD8
#define NOR_CMD_CHIP_ERASE          0xC7
#define NOR_CMD_POWER_DOWN          0xB9
#define NOR_CMD_RELEASE_PD          0xAB
#define NOR_CMD_READ_JEDEC_ID       0x9F

/* NOR Flash状态位 */
#define NOR_STATUS_BUSY             0x01
#define NOR_STATUS_WEL              0x02

/* NOR Flash参数 */
#define NOR_PAGE_SIZE               256
#define NOR_SECTOR_SIZE_4K          4096
#define NOR_BLOCK_SIZE_32K          32768
#define NOR_BLOCK_SIZE_64K          65536

/*===========================================================================
 * NAND Flash命令定义 (W25N02)
 *===========================================================================*/
#define NAND_CMD_RESET              0xFF
#define NAND_CMD_READ_JEDEC_ID      0x9F
#define NAND_CMD_READ_ID            0x90
#define NAND_CMD_GET_FEATURE        0x0F
#define NAND_CMD_SET_FEATURE        0x1F
#define NAND_CMD_WRITE_ENABLE       0x06
#define NAND_CMD_WRITE_DISABLE      0x04
#define NAND_CMD_PAGE_DATA_READ     0x13
#define NAND_CMD_READ_DATA          0x03
#define NAND_CMD_PROGRAM_LOAD       0x02
#define NAND_CMD_PROGRAM_EXECUTE    0x10
#define NAND_CMD_BLOCK_ERASE        0xD8

/* NAND Flash寄存器地址 */
#define NAND_REG_PROTECTION         0xA0
#define NAND_REG_CONFIGURATION      0xB0
#define NAND_REG_STATUS             0xC0

/* NAND Flash状态位 */
#define NAND_STATUS_BUSY            0x01
#define NAND_STATUS_WEL             0x02
#define NAND_STATUS_EFAIL           0x04
#define NAND_STATUS_PFAIL           0x08
#define NAND_STATUS_ECC1            0x20
#define NAND_STATUS_ECC2            0x40

/* NAND Flash参数 (W25N02) */
#define NAND_PAGE_SIZE              2048
#define NAND_PAGE_SPARE_SIZE        64
#define NAND_PAGES_PER_BLOCK        64
#define NAND_BLOCK_SIZE             (NAND_PAGE_SIZE * NAND_PAGES_PER_BLOCK)
#define NAND_W25N02_BLOCK_COUNT     1024
#define NAND_W25N02_TOTAL_SIZE      (256 * 1024 * 1024)  /* 256MB */

/*===========================================================================
 * 驱动创建函数
 *===========================================================================*/

/**
 * 创建NOR Flash驱动实例
 * @param id 设备ID
 * @param cs_enable CS使能函数
 * @param cs_disable CS禁用函数
 * @return 驱动实例指针，失败返回NULL
 */
FlashDriver_t* FlashDriver_CreateNOR(uint8_t id, FlashCsFunc_t cs_enable, FlashCsFunc_t cs_disable);

/**
 * 创建NAND Flash驱动实例
 * @param id 设备ID
 * @param cs_enable CS使能函数
 * @param cs_disable CS禁用函数
 * @return 驱动实例指针，失败返回NULL
 */
FlashDriver_t* FlashDriver_CreateNAND(uint8_t id, FlashCsFunc_t cs_enable, FlashCsFunc_t cs_disable);

/**
 * 销毁Flash驱动实例
 * @param drv 驱动实例
 */
void FlashDriver_Destroy(FlashDriver_t *drv);

/*===========================================================================
 * 底层SPI通信函数（内部使用）
 *===========================================================================*/

void flash_spi_write_byte(uint8_t data);
uint8_t flash_spi_read_byte(void);
void flash_spi_write(const uint8_t *data, uint16_t len);
void flash_spi_read(uint8_t *data, uint16_t len);

#endif /* __FLASH_DRIVER_H__ */
