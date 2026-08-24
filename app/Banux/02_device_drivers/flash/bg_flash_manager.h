#ifndef __BG_FLASH_MANAGER_H__
#define __BG_FLASH_MANAGER_H__

#include <stdint.h>
#include <stdbool.h>
#include "gpio.h"

#define DEV_NOR1 	0   // 第一个NOR Flash, CS = GPIOA21
#define DEV_NOR2 	1   // 第二个NOR Flash, CS = GPIOA22

// 兼容旧代码
#define DEV_NOR     DEV_NOR1
#define DEV_NAND    DEV_NOR2

// 第一个NOR Flash CS引脚 (GPIOA21)
#define NOR1_CS_INIT()        GPIO_RegOneBitClear(GPIO_A_IE, GPIOA21);\
		                      GPIO_RegOneBitSet(GPIO_A_OE, GPIOA21);\
		                      GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA21);

// 第二个NOR Flash CS引脚 (GPIOA22)
#define NOR2_CS_INIT()        GPIO_RegOneBitClear(GPIO_A_IE, GPIOA22);\
		                      GPIO_RegOneBitSet(GPIO_A_OE, GPIOA22);\
		                      GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22);

#define NOR1_CS_ENABLE()      GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA21);
#define NOR1_CS_DISABLE()     GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA21);

#define NOR2_CS_ENABLE()      GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA22);
#define NOR2_CS_DISABLE()     GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22);

// 兼容旧代码的宏
#define FLASH_CS_INIT()       NOR1_CS_INIT()
#define FLASH_CS_ENABLE()     NOR1_CS_ENABLE()
#define FLASH_CS_DISABLE()    NOR1_CS_DISABLE()
#define NAND_CS_INIT()        NOR2_CS_INIT()
#define NAND_CS_ENABLE()      NOR2_CS_ENABLE()
#define NAND_CS_DISABLE()     NOR2_CS_DISABLE()


//#define FLASH_HOLD_INIT()      GPIO_RegOneBitClear(GPIO_A_IE, GPIOA22);\
//		                       GPIO_RegOneBitSet(GPIO_A_OE, GPIOA22);\
//		                       GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22);
//
//#define FLASH_HOLD_ENABLE()     GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA22);
//#define FLASH_HOLD_DISABLE()    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA22);


#define FLASH_WP_INIT()      GPIO_RegOneBitClear(GPIO_A_IE, GPIOA17);\
		                       GPIO_RegOneBitSet(GPIO_A_OE, GPIOA17);\
		                       GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA17);

#define FLASH_WP_ENABLE()     GPIO_RegOneBitClear(GPIO_A_OUT, GPIOA17);
#define FLASH_WP_DISABLE()    GPIO_RegOneBitSet(GPIO_A_OUT, GPIOA17);

#define FLASH_STATUS_OK 0
#define FLASH_STATUS_ERROR 1

#define SECTOR_SIZE (4 * 1024*8) // ÿ�������Ĵ�С��32KBit

#define DEVICE_ID_64MBIT  0x17
#define DEVICE_ID_128MBIT 0x16
#define DEVICE_ID_256MBIT 0x14
#define DEVICE_ID_512MBIT 0x13
#define DEVICE_ID_1GBIT   0x12
#define DEVICE_ID_2GBIT   0x22  // W25N02 2Gbit NAND Flash
#define DEVICE_ID_W25N02  0xAA  // W25N02 alternative device ID

#define PAGE_SIZE 256
#define NAND_PAGE_SIZE 2048  // W25N02 NAND Flash页面大小
// FLASH�����
#define FLASH_CMD_JEDEC_ID 	   0x9F
#define FLASH_CMD_WRITE_ENABLE     0x06
#define FLASH_CMD_WRITE_DISABLE    0x04
#define FLASH_CMD_READ_STATUS_REG  0x05
#define FLASH_CMD_WRITE_STATUS_REG 0x01
#define FLASH_CMD_READ_DATA        0x03
#define FLASH_CMD_FAST_READ        0x0B
#define FLASH_CMD_PAGE_PROGRAM     0x02
#define FLASH_CMD_SECTOR_ERASE     0xD8
#define FLASH_CMD_BLOCK_ERASE_32K  0x52
#define FLASH_CMD_BLOCK_ERASE_64K  0xD8
#define FLASH_CMD_CHIP_ERASE       0xC7
#define FLASH_CMD_POWER_DOWN       0xB9
#define FLASH_CMD_RELEASE_POWER_DOWN 0xAB
#define FLASH_CMD_JEDEC_ID         0x9F
#define NAND_CMD_JEDEC_ID         0x90
#define NAND_CMD_PAGE_DATA_READ   0x13  // W25N02 Page Data Read命令
#define NAND_CMD_RANDOM_DATA_READ 0x03  // W25N02 Random Data Read命令
#define NAND_CMD_PROGRAM_LOAD     0x02  // W25N02 Program Load命令
#define NAND_CMD_PROGRAM_EXECUTE  0x10  // W25N02 Program Execute命令
#define NAND_CMD_BLOCK_ERASE      0xD8  // W25N02 Block Erase命令
#define NAND_CMD_GET_FEATURE      0x0F  // W25N02 Get Feature命令（读状态）
#define NAND_CMD_SET_FEATURE      0x1F  // W25N02 Set Feature命令（写状态）

typedef struct{

	void (*Init)(void);
	uint8_t (*PageProgram)(uint32_t,uint8_t*,uint16_t,uint8_t);
	void (*WriteEnable)(uint8_t,uint8_t);
	void (*SectorErase)(uint32_t,uint8_t);
	void (*ReadData)(uint32_t,uint8_t*,uint16_t,uint8_t);
	void (*ReadID)(uint8_t*, uint8_t* , uint8_t*,uint8_t);
	uint32_t (*GetRemainingCapacity)(uint8_t);
	uint32_t (*GetTotalByte)(uint8_t);
	void (*EraseAll)(uint8_t);

}BG_Flash_Manager;

extern BG_Flash_Manager BG_flash_manager;

// 坏块管理函数声明
uint8_t nand_check_bad_block(uint32_t block_address, uint8_t dev);
uint8_t nand_mark_bad_block(uint32_t block_address, uint8_t dev);
uint32_t nand_find_next_good_block(uint32_t start_block, uint8_t dev);
uint32_t nand_get_safe_write_address(uint32_t current_address, uint32_t bytes_to_write, uint8_t dev);

// NAND Flash智能音频函数
void nand_smart_audio_init(void);
uint8_t nand_smart_audio_write(uint32_t address, uint8_t* data, uint16_t size, uint8_t dev);
uint8_t nand_smart_audio_read(uint32_t address, uint8_t* data, uint16_t size, uint8_t dev);
uint8_t nand_smart_audio_flush(uint8_t dev);
uint32_t nand_smart_audio_get_address(void);

// 测试函数声明
void flash_test_w25n02_registers(void);
void flash_test_spi_loopback(void);

#endif
