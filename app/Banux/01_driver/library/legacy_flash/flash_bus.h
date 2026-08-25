/**
 * flash_bus.h - Flash总线管理器
 * 
 * 采用总线-驱动模型：
 * - 驱动注册到总线
 * - 统一管理所有Flash设备
 * - 提供Shell接口
 */

#ifndef __FLASH_BUS_H__
#define __FLASH_BUS_H__

#include <stdint.h>
#include <stdbool.h>

/*===========================================================================
 * 常量定义
 *===========================================================================*/

#define FLASH_BUS_MAX_DEVICES       8       /* 总线最大设备数 */
#define FLASH_NAME_MAX_LEN          16      /* 设备名称最大长度 */
#define FLASH_DEV_NAME_MAX          FLASH_NAME_MAX_LEN  /* 兼容别名 */

/*===========================================================================
 * 状态码定义
 *===========================================================================*/

typedef enum {
    FLASH_OK = 0,
    FLASH_ERR_PARAM,
    FLASH_ERR_BUSY,
    FLASH_ERR_TIMEOUT,
    FLASH_ERR_NOT_FOUND,
    FLASH_ERR_NOT_INIT,
    FLASH_ERR_BAD_BLOCK,
    FLASH_ERR_ECC,
    FLASH_ERR_PROGRAM,
    FLASH_ERR_ERASE,
    FLASH_ERR_VERIFY,
    FLASH_ERR_FULL,
    FLASH_ERR_NOMEM,
    FLASH_ERR_READ,
    FLASH_ERR_WRITE
} FlashStatus_t;

/*===========================================================================
 * Flash类型定义 (兼容audio_looper.h)
 *===========================================================================*/

#ifndef FLASH_TYPE_DEFINED
#define FLASH_TYPE_DEFINED
typedef enum {
    FLASH_TYPE_NOR = 0,
    FLASH_TYPE_NAND,
    FLASH_TYPE_PSRAM,       /* PSRAM (ESP-PSRAM64H etc.) */
    FLASH_TYPE_SDCARD,      /* SD Card (SDIO interface) */
    FLASH_TYPE_MAX
} FlashType_t;
#endif /* FLASH_TYPE_DEFINED */

/*===========================================================================
 * Flash设备信息
 *===========================================================================*/

typedef struct {
    uint8_t  mfg_id;            /* 制造商ID */
    uint8_t  mem_type;          /* 内存类型 */
    uint8_t  dev_id;            /* 设备ID */
    uint32_t total_size;        /* 总容量(字节) */
    uint32_t page_size;         /* 页大小 */
    uint32_t sector_size;       /* 扇区大小 */
    uint32_t block_size;        /* 块大小 */
    uint16_t block_count;       /* 块数量 */
} FlashDevInfo_t;

/*===========================================================================
 * Flash驱动操作接口
 *===========================================================================*/

/* 前向声明 */
typedef struct FlashDevice FlashDevice_t;

/* 驱动操作函数指针类型 */
typedef struct {
    /* 初始化/反初始化 */
    FlashStatus_t (*init)(FlashDevice_t *dev);
    FlashStatus_t (*deinit)(FlashDevice_t *dev);
    
    /* 读写操作 */
    FlashStatus_t (*read)(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len);
    FlashStatus_t (*write)(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len);
    
    /* 擦除操作 */
    FlashStatus_t (*erase_sector)(FlashDevice_t *dev, uint32_t addr);
    FlashStatus_t (*erase_block)(FlashDevice_t *dev, uint32_t addr);
    FlashStatus_t (*erase_chip)(FlashDevice_t *dev);
    
    /* 状态操作 */
    FlashStatus_t (*get_status)(FlashDevice_t *dev, uint8_t *status);
    FlashStatus_t (*wait_ready)(FlashDevice_t *dev, uint32_t timeout_ms);
    
    /* 信息获取 */
    FlashStatus_t (*read_id)(FlashDevice_t *dev);
    FlashStatus_t (*get_info)(FlashDevice_t *dev, FlashDevInfo_t *info);
} FlashOps_t;

/*===========================================================================
 * CS引脚控制
 *===========================================================================*/

typedef struct {
    void (*init)(void);         /* 初始化CS引脚 */
    void (*select)(void);       /* 选中设备 (CS低) */
    void (*deselect)(void);     /* 取消选中 (CS高) */
} FlashCS_t;

/*===========================================================================
 * Flash设备结构
 *===========================================================================*/

struct FlashDevice {
    /* 设备标识 */
    char            name[FLASH_NAME_MAX_LEN];   /* 设备名称 */
    uint8_t         id;                         /* 设备ID (总线分配) */
    FlashType_t     type;                       /* Flash类型 */
    bool            initialized;                /* 是否已初始化 */
    bool            registered;                 /* 是否已注册 */
    
    /* 设备信息 */
    FlashDevInfo_t  info;                       /* 设备信息 */
    
    /* 硬件控制 */
    FlashCS_t       cs;                         /* CS引脚控制 */
    
    /* 驱动操作 */
    const FlashOps_t *ops;                      /* 操作函数 */
    
    /* 私有数据 */
    void            *priv;                      /* 驱动私有数据 */
    
    /* 链表 */
    FlashDevice_t   *next;                      /* 下一个设备 */
};

/*===========================================================================
 * Flash总线结构
 *===========================================================================*/

typedef struct {
    bool            initialized;                /* 总线是否初始化 */
    uint8_t         device_count;               /* 已注册设备数 */
    FlashDevice_t   *head;                      /* 设备链表头 */
    FlashDevice_t   *devices[FLASH_BUS_MAX_DEVICES]; /* 设备数组(按ID索引) */
} FlashBus_t;

/*===========================================================================
 * 总线API
 *===========================================================================*/

/**
 * 初始化Flash总线
 */
FlashStatus_t FlashBus_Init(void);

/**
 * 反初始化Flash总线
 */
void FlashBus_DeInit(void);

/**
 * 获取总线实例
 */
FlashBus_t* FlashBus_GetInstance(void);

/**
 * 注册设备到总线
 * @param dev 设备指针
 * @return FLASH_OK成功
 */
FlashStatus_t FlashBus_Register(FlashDevice_t *dev);

/**
 * 从总线注销设备
 * @param dev 设备指针
 * @return FLASH_OK成功
 */
FlashStatus_t FlashBus_Unregister(FlashDevice_t *dev);

/**
 * 根据ID获取设备
 * @param id 设备ID
 * @return 设备指针，失败返回NULL
 */
FlashDevice_t* FlashBus_GetDeviceById(uint8_t id);

/**
 * 根据名称获取设备
 * @param name 设备名称
 * @return 设备指针，失败返回NULL
 */
FlashDevice_t* FlashBus_GetDeviceByName(const char *name);

/**
 * 获取设备数量
 */
uint8_t FlashBus_GetDeviceCount(void);

/**
 * 遍历所有设备
 * @param callback 回调函数
 * @param user_data 用户数据
 */
void FlashBus_ForEach(void (*callback)(FlashDevice_t *dev, void *user_data), void *user_data);

/*===========================================================================
 * 设备操作便捷API
 *===========================================================================*/

/**
 * 初始化设备
 */
FlashStatus_t FlashDev_Init(FlashDevice_t *dev);

/**
 * 读取数据
 */
FlashStatus_t FlashDev_Read(FlashDevice_t *dev, uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * 写入数据
 */
FlashStatus_t FlashDev_Write(FlashDevice_t *dev, uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * 擦除扇区
 */
FlashStatus_t FlashDev_EraseSector(FlashDevice_t *dev, uint32_t addr);

/**
 * 擦除块
 */
FlashStatus_t FlashDev_EraseBlock(FlashDevice_t *dev, uint32_t addr);

/**
 * 全片擦除
 */
FlashStatus_t FlashDev_EraseChip(FlashDevice_t *dev);

/**
 * 打印设备信息
 */
void FlashDev_PrintInfo(FlashDevice_t *dev);

/*===========================================================================
 * Shell命令接口
 *===========================================================================*/

/**
 * 注册Flash Shell命令
 */
void FlashBus_RegisterShellCommands(void);

/**
 * Flash Shell命令处理
 * @param argc 参数数量
 * @param argv 参数数组
 * @return 0成功，其他失败
 */
int FlashBus_ShellCmd(int argc, char *argv[]);

/*===========================================================================
 * 调试接口
 *===========================================================================*/

/**
 * 打印总线信息
 */
void FlashBus_PrintInfo(void);

/**
 * 测试设备
 */
FlashStatus_t FlashBus_TestDevice(uint8_t id);

#endif /* __FLASH_BUS_H__ */
