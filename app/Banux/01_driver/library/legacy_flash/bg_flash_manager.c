#include "bg_flash_manager.h"
#include "spim.h"
#include "spi_flash.h"
#include "debug.h"
#include "spim_interface.h"
#include "dma.h"
#include "banux_config.h"
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// 鍧忓潡绠＄悊鐩稿叧瀹氫箟
#define BAD_BLOCK_MARKER 0x00  // 鍧忓潡鏍囪锛屾甯稿潡搴斾负0xFF
#define BAD_BLOCK_TABLE_MAGIC 0x42424242  // 鍧忓潡琛ㄩ瓟鏈瓧 "BBBB"
#define BAD_BLOCK_TABLE_VERSION 0x0101    // 鍧忓潡琛ㄧ増鏈�

// 鍧忓潡琛ㄧ粨鏋�
typedef struct {
    uint32_t magic;               // 榄旀湳瀛楋紝鐢ㄤ簬楠岃瘉鍧忓潡琛�
    uint16_t version;             // 鐗堟湰鍙�
    uint16_t count;               // 鍧忓潡鏁伴噺
    uint32_t bad_blocks[128];     // 鍧忓潡鍦板潃鍒楄〃锛屾渶澶氭敮鎸�28涓潖鍧�
    uint32_t reserved[4];         // 棰勭暀绌洪棿
} BadBlockTable;

// 鍧忓潡绠＄悊鐘舵�
typedef struct {
    BadBlockTable table;          // 鍧忓潡琛�
    uint32_t table_address;       // 鍧忓潡琛ㄥ瓨鍌ㄥ湴鍧�
    bool initialized;             // 鍧忓潡绠＄悊鏄惁宸插垵濮嬪寲
} BadBlockManager;

// 鍐呴儴鍑芥暟澹版槑
void flash_init(void);
void flash_ReadID(uint8_t* manufacturerID, uint8_t* memoryType, uint8_t* deviceID, uint8_t dev);
void flash_WriteEnable(uint8_t enable,uint8_t dev);
uint8_t flash_ReadStatusReg(uint8_t dev);
void flash_WriteStatusReg(uint8_t data,uint8_t dev);
void flash_WaitForWriteEnd(uint8_t dev);
void flash_SectorErase(uint32_t sectorAddress,uint8_t dev);
uint8_t flash_PageProgram(uint32_t address, uint8_t* data, uint16_t size,uint8_t dev);
void flash_ReadData(uint32_t address, uint8_t* data, uint16_t size,uint8_t dev);
uint32_t flash_GetRemainingCapacity(uint8_t dev);
uint32_t flash_GetTotalByte(uint8_t dev);
void flash_EraseAll(uint8_t dev);
void flash_write_byte(uint8_t data);
uint8_t flash_read_byte(void);
void flash_write(uint8_t* data,uint16_t size);
void flash_read(uint8_t* data,uint16_t size);

// 鍧忓潡绠＄悊鍑芥暟澹版槑
static void bad_block_manager_init(uint8_t dev);
static bool is_block_bad(uint32_t block_address, uint8_t dev);
static bool mark_block_as_bad(uint32_t block_address, uint8_t dev);
static uint32_t find_next_good_block(uint32_t start_block, uint8_t dev);
static void save_bad_block_table(uint8_t dev);
static void load_bad_block_table(uint8_t dev);
static uint32_t address_to_block(uint32_t address, uint8_t dev);
static uint32_t block_to_address(uint32_t block, uint8_t dev);

// 鏅鸿兘闊抽缂撳啿鍖哄嚱鏁板０鏄�
static uint8_t nand_audio_flush_buffer(uint8_t dev);

// 鍏ㄥ眬鍙橀噺
BG_Flash_Manager BG_flash_manager = {
	.Init = flash_init,
	.PageProgram = flash_PageProgram,
	.SectorErase = flash_SectorErase,
	.WriteEnable = flash_WriteEnable,
	.ReadData = flash_ReadData,
	.ReadID = flash_ReadID,
	.GetRemainingCapacity = flash_GetRemainingCapacity,
	.GetTotalByte = flash_GetTotalByte,
	.EraseAll = flash_EraseAll,
};

// 鍧忓潡绠＄悊鍣ㄥ疄渚�
static BadBlockManager bad_block_manager = {0};

void flash_init(void)
{
	// 鍒濆鍖栦袱涓�NOR Flash 鐨�CS 寮曡剼
	FLASH_CS_INIT();   // NOR1 (GPIOA21)
	NAND_CS_INIT();    // NOR2 (GPIOA22)
#ifndef BANBOX_II
	/* BanBox II uses A17 for SDIO CMD — skip WP init to avoid conflict */
	FLASH_WP_INIT();
#endif
	FLASH_CS_DISABLE();
	NAND_CS_DISABLE();
#ifndef BANBOX_II
	FLASH_WP_DISABLE();
#endif

	DBG("Dual NOR Flash initialized (CS=A21, A22)\n");
}

// 鍚屾椂鍐欏叆鍜岃鍙栦竴涓瓧鑺傦紙鐢ㄤ簬娴嬭瘯SPI杩炴帴锛�
// 褰撳墠鏈娇鐢紝淇濈暀澶囩敤
/*
static uint8_t flash_write_read_byte(uint8_t data) {
	uint8_t received = 0;

	// 鍙戦�鏁版嵁骞跺悓鏃舵帴鏀�
	SPIM_DMA_Send_Start(&data, 1);
	SPIM_DMA_Recv_Start(&received, 1);

	// 绛夊緟鍙戦�瀹屾垚
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));

	// 绛夊緟鎺ユ敹瀹屾垚
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));

	return received;
}
*/

void flash_write_byte(uint8_t data){
	SPIM_DMA_Send_Start(&data, 1);
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

uint8_t flash_read_byte(void){
	uint8_t data;
	SPIM_DMA_Recv_Start(&data,1);
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
	return data;
}

void flash_read(uint8_t* data,uint16_t size){
	SPIM_DMA_Recv_Start(data,size);
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_RX));
}

void flash_write(uint8_t* data,uint16_t size){
	SPIM_DMA_Send_Start(data, size);
	while(!SPIM_DMA_HalfDone(PERIPHERAL_ID_SPIM_TX));
}

void flash_ReadID(uint8_t* manufacturerID, uint8_t* memoryType, uint8_t* deviceID, uint8_t dev) {
  // 涓や釜璁惧閮芥槸 NOR Flash锛屼娇鐢ㄧ浉鍚岀殑璇诲彇鏂规硶
  if(dev==DEV_NOR1){
	FLASH_CS_ENABLE();
	flash_write_byte(FLASH_CMD_JEDEC_ID);

	*manufacturerID = flash_read_byte();
	*memoryType = flash_read_byte();
	*deviceID = flash_read_byte();

	FLASH_CS_DISABLE();
  }
  else if(dev==DEV_NOR2){
	// 绗簩涓�NOR Flash锛屼篃浣跨敤鏍囧噯 JEDEC ID 璇诲彇
	NAND_CS_ENABLE();  // 浣跨敤 A22 寮曡剼
	flash_write_byte(FLASH_CMD_JEDEC_ID);

	*manufacturerID = flash_read_byte();
	*memoryType = flash_read_byte();
	*deviceID = flash_read_byte();

	NAND_CS_DISABLE();
  }
}


uint32_t flash_GetTotalByte(uint8_t dev) {
    uint32_t capacity = 0;
    uint8_t manufacturerID, memoryType, deviceID;
    flash_ReadID(&manufacturerID, &memoryType, &deviceID,dev);

    // 娣诲姞璋冭瘯淇℃伅鏄剧ず璇诲彇鍒扮殑ID
    DBG("%s Flash ID: Manufacturer=0x%02X, MemoryType=0x%02X, DeviceID=0x%02X\n",
        dev == DEV_NOR1 ? "NOR1" : "NOR2", manufacturerID, memoryType, deviceID);

    // 涓や釜璁惧閮芥槸 NOR Flash锛屾寜璁惧ID鍖归厤瀹归噺
    switch (deviceID) {
            case DEVICE_ID_64MBIT:
                capacity = 64 * 1024 * 1024; // 64 Mbit
                break;
            case DEVICE_ID_128MBIT:
                capacity = 128 * 1024 * 1024; // 128 Mbit
                break;
            case DEVICE_ID_256MBIT:
                capacity = 256 * 1024 * 1024; // 256 Mbit
                break;
            case DEVICE_ID_512MBIT:
                capacity = 512 * 1024 * 1024; // 512 Mbit
                break;
            case DEVICE_ID_1GBIT:
                capacity = 1024 * 1024 * 1024; // 1 Gbit
                break;
            case DEVICE_ID_2GBIT:
                capacity = 2048 * 1024 * 1024; // 2 Gbit (W25N02)
                break;
            case DEVICE_ID_W25N02:  // 0xAA 涔熷彲鑳芥槸璁惧ID
                capacity = 256 * 1024 * 1024; // W25N02 256MB
                break;
            default:
                // 鏈煡璁惧ID锛屽皾璇曟牴鎹埗閫犲晢鍜岀被鍨嬫帹鏂�
                if (dev == DEV_NAND && manufacturerID == 0xEF) {
                    // Winbond NAND Flash锛屼絾璁惧ID鏈煡
                    DBG("Unknown Winbond NAND device ID: 0x%02X, assuming W25N02\n", deviceID);
                    capacity = 256 * 1024 * 1024; // 榛樿涓篧25N02瀹归噺
                } else {
                    DBG("Unknown device ID: 0x%02X\n", deviceID);
                    capacity = 0;
                }
                break;
        }


        // 瀵逛簬NOR Flash锛屽彲鑳介渶瑕乥it鍒癰yte鐨勮浆鎹�
        return capacity/8;

}

uint32_t Windbond_GetCapacity(uint8_t deviceID,uint8_t dev) {
    uint32_t capacity = 0;

    switch (deviceID) {
        case DEVICE_ID_64MBIT:
            capacity = 64 * 1024 * 1024; // 64 Mbit
            break;
        case DEVICE_ID_128MBIT:
            capacity = 128 * 1024 * 1024; // 128 Mbit
            break;
        case DEVICE_ID_256MBIT:
            capacity = 256 * 1024 * 1024; // 256 Mbit
            break;
        case DEVICE_ID_512MBIT:
            capacity = 512 * 1024 * 1024; // 512 Mbit
            break;
        case DEVICE_ID_1GBIT:
            capacity = 1024 * 1024 * 1024; // 1 Gbit
            break;
        case DEVICE_ID_2GBIT:
            capacity = 2048 * 1024 * 1024; // 2 Gbit (W25N02)
            break;
        default:
            capacity = 0;
            break;
    }

    return capacity;
}

bool flash_IsSectorErased(uint32_t sectorAddress,uint8_t dev) {
    uint8_t data;
    // 璇诲彇鎵囧尯鐨勭涓�釜瀛楄妭
    flash_ReadData(sectorAddress, &data, 1,dev);
    // 濡傛灉绗竴涓瓧鑺傛槸0xFF鍒欒涓烘墖鍖哄凡鎿﹂櫎
    return data == 0xFF;
}

uint32_t flash_GetRemainingCapacity(uint8_t dev) {
	uint32_t remainingCapacity = 0;
    uint32_t sectorAddress = 0;
    uint32_t i;
    uint8_t manufacturerID, memoryType, deviceID;
    flash_ReadID(&manufacturerID, &memoryType, &deviceID,dev);
    for (i = 0; i < Windbond_GetCapacity(deviceID,dev)/SECTOR_SIZE ; ++i) {
        // 涓や釜璁惧閮芥槸 NOR Flash锛屾棤闇�鏌ュ潖鍧�

        if (flash_IsSectorErased(sectorAddress,dev)) {
            remainingCapacity += 1 ;
        }
        // 绉诲姩鍒颁笅涓�釜鎵囧尯
        sectorAddress += SECTOR_SIZE;
    }
    DBG("Total is:%d KByte,Remain is:%d KByte\n",(Windbond_GetCapacity(deviceID,dev)/SECTOR_SIZE)*4,remainingCapacity*4);
    return remainingCapacity;
}

void flash_WriteEnable(uint8_t enable,uint8_t dev) {
	if(dev==DEV_NOR1){
		FLASH_CS_ENABLE();
		if(enable){
			flash_write_byte(FLASH_CMD_WRITE_ENABLE);
		}else{
			flash_write_byte(FLASH_CMD_WRITE_DISABLE);
		}
		FLASH_CS_DISABLE();
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勫啓浣胯兘鎿嶄綔
		NAND_CS_ENABLE();
		if(enable){
			flash_write_byte(FLASH_CMD_WRITE_ENABLE);
		}else{
			flash_write_byte(FLASH_CMD_WRITE_DISABLE);
		}
		NAND_CS_DISABLE();
	}


}

// 璇诲彇鐘舵�瀵勫瓨鍣�
uint8_t flash_ReadStatusReg(uint8_t dev) {
    uint8_t data;
    if(dev==DEV_NOR1){
		FLASH_CS_ENABLE();
		flash_write_byte(FLASH_CMD_READ_STATUS_REG);
		data = flash_read_byte();
		FLASH_CS_DISABLE();
    }
    else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勬爣鍑嗘搷浣�
		NAND_CS_ENABLE();
		flash_write_byte(FLASH_CMD_READ_STATUS_REG);
		data = flash_read_byte();
		NAND_CS_DISABLE();
    }
    return data;
}

// 鍐欑姸鎬佸瘎瀛樺櫒
void flash_WriteStatusReg(uint8_t data,uint8_t dev) {
	if(dev==DEV_NOR1){
		flash_WriteEnable(1,dev);
		FLASH_CS_ENABLE();
		flash_write_byte(FLASH_CMD_WRITE_STATUS_REG);
		flash_write_byte(data);
		FLASH_CS_DISABLE();
		flash_WaitForWriteEnd(dev);
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勬爣鍑嗘搷浣�
		flash_WriteEnable(1,dev);
		NAND_CS_ENABLE();
		flash_write_byte(FLASH_CMD_WRITE_STATUS_REG);
		flash_write_byte(data);
		NAND_CS_DISABLE();
		flash_WaitForWriteEnd(dev);
	}
}

// 绛夊緟鍐欏叆瀹屾垚
void flash_WaitForWriteEnd(uint8_t dev) {
    // 涓や釜璁惧閮芥槸 NOR Flash锛屼娇鐢ㄧ浉鍚岀殑绛夊緟鏂瑰紡
    while ((flash_ReadStatusReg(dev) & 0x01) == 0x01);
}

// 鎵囧尯鎿﹂櫎
void flash_SectorErase(uint32_t sectorAddress,uint8_t dev) {
	if(dev==DEV_NOR1){
		flash_WriteEnable(1,dev);
		FLASH_CS_ENABLE();
		flash_write_byte(FLASH_CMD_SECTOR_ERASE);
		flash_write_byte((sectorAddress & 0xFF0000) >> 16);
		flash_write_byte((sectorAddress & 0x00FF00) >> 8);
		flash_write_byte(sectorAddress & 0x0000FF);
		FLASH_CS_DISABLE();
		flash_WaitForWriteEnd(dev);
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勬爣鍑嗘搷浣�
		flash_WriteEnable(1,dev);
		NAND_CS_ENABLE();
		flash_write_byte(FLASH_CMD_SECTOR_ERASE);
		flash_write_byte((sectorAddress & 0xFF0000) >> 16);
		flash_write_byte((sectorAddress & 0x00FF00) >> 8);
		flash_write_byte(sectorAddress & 0x0000FF);
		NAND_CS_DISABLE();
		flash_WaitForWriteEnd(dev);
	}
}

void flash_EraseAll(uint8_t dev) {
	if(dev==DEV_NOR){
    flash_WriteEnable(1,dev);
    FLASH_CS_ENABLE();
    flash_write_byte(FLASH_CMD_CHIP_ERASE);
    FLASH_CS_DISABLE();
    flash_WaitForWriteEnd(dev);
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勫叏鐗囨摝闄�
		flash_WriteEnable(1,dev);
		NAND_CS_ENABLE();
		flash_write_byte(FLASH_CMD_CHIP_ERASE);
		NAND_CS_DISABLE();
		flash_WaitForWriteEnd(dev);
	}
}

// 椤电紪绋�
uint8_t flash_PageProgram(uint32_t address, uint8_t* data, uint16_t size,uint8_t dev) {
	if(dev==DEV_NOR1){
 	 	flash_WriteEnable(1,dev);  // 浣胯兘鍐欐搷浣�
        FLASH_CS_ENABLE();     // 閫夋嫨W25Q64
        flash_write_byte(FLASH_CMD_PAGE_PROGRAM);  // 鍙戦�椤电紪绋嬫寚浠�
        flash_write_byte((address >> 16) & 0xFF);  // 鍙戦�鍦板潃鐨勯珮瀛楄妭
        flash_write_byte((address >> 8) & 0xFF);   // 鍙戦�鍦板潃鐨勪腑瀛楄妭
        flash_write_byte(address & 0xFF);          // 鍙戦�鍦板潃鐨勪綆瀛楄妭

        flash_write(data,size);
        FLASH_CS_DISABLE();
        flash_WaitForWriteEnd(dev);  // 绛夊緟鍐欐搷浣滃畬鎴�
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勯〉缂栫▼鎿嶄綔
 	 	flash_WriteEnable(1,dev);
        NAND_CS_ENABLE();
        flash_write_byte(FLASH_CMD_PAGE_PROGRAM);
        flash_write_byte((address >> 16) & 0xFF);
        flash_write_byte((address >> 8) & 0xFF);
        flash_write_byte(address & 0xFF);

        flash_write(data,size);
        NAND_CS_DISABLE();
        flash_WaitForWriteEnd(dev);
	}
	return FLASH_STATUS_OK;  // 杩斿洖鎴愬姛鐘舵�
}

// 璇诲彇鏁版嵁
void flash_ReadData(uint32_t address, uint8_t* data, uint16_t size ,uint8_t dev) {
	if(dev==DEV_NOR1){
		FLASH_CS_ENABLE();
		flash_write_byte(FLASH_CMD_READ_DATA);
		flash_write_byte((address & 0xFF0000) >> 16);
		flash_write_byte((address & 0xFF00) >> 8);
		flash_write_byte(address & 0xFF);

		flash_read(data,size);
		FLASH_CS_DISABLE();
	}
	else if(dev==DEV_NOR2){
		// 绗簩涓�NOR Flash 浣跨敤鐩稿悓鐨勮鍙栨搷浣�
		NAND_CS_ENABLE();
		flash_write_byte(FLASH_CMD_READ_DATA);
		flash_write_byte((address & 0xFF0000) >> 16);
		flash_write_byte((address & 0xFF00) >> 8);
		flash_write_byte(address & 0xFF);

		flash_read(data,size);
		NAND_CS_DISABLE();
	}
}

// W25N02 NAND Flash 瀵勫瓨鍣ㄦ祴璇曞嚱鏁�
void flash_test_w25n02_registers(void) {
	int i;
	uint8_t status, prot_reg, conf_reg, stat_reg, drv_reg;
	uint8_t mfg, type, dev, nand_status, protection, config;
	uint32_t capacity;
	uint8_t test_data[16] = {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA,
							 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
	uint8_t read_buffer[16];
	uint32_t test_address = 0x1000;
	bool test_passed = true;
	bool all_zero;
	
	DBG("寮�W25N02 NAND Flash绯荤粺娴嬭瘯...\n");
	DBG("========== W25N02 Register Test ==========\n");
	
	// 纭欢杩炴帴娴嬭瘯
	DBG("=== Hardware Connection Test ===\n");
	DBG("NAND CS Pin State Test:\n");
	NAND_CS_ENABLE();
	DBG("CS Enabled\n");
	NAND_CS_DISABLE(); 
	DBG("CS Disabled\n");
	
	// SPI閫氫俊娴嬭瘯
	DBG("=== SPI Communication Test ===\n");
	DBG("Testing basic SPI response...\n");
	
	// 澶氭娴嬭瘯鐘舵�瀵勫瓨鍣�
	DBG("Testing status register multiple times...\n");
	for(i = 1; i <= 5; i++) {
		status = flash_ReadStatusReg(DEV_NAND);
		DBG("Status test %d: 0x%02X\n", i, status);
	}
	
	// 娴嬭瘯涓嶅悓鐨勭壒鎬у瘎瀛樺櫒
	DBG("Testing different feature registers...\n");
	NAND_CS_ENABLE();
	flash_write_byte(0x0F); // Get Features command
	flash_write_byte(0xA0); // Protection register
	prot_reg = flash_read_byte();
	NAND_CS_DISABLE();
	DBG("Register 0xA0: 0x%02X\n", prot_reg);
	
	NAND_CS_ENABLE();
	flash_write_byte(0x0F);
	flash_write_byte(0xB0); // Configuration register
	conf_reg = flash_read_byte();
	NAND_CS_DISABLE();
	DBG("Register 0xB0: 0x%02X\n", conf_reg);
	
	NAND_CS_ENABLE();
	flash_write_byte(0x0F);
	flash_write_byte(0xC0); // Status register
	stat_reg = flash_read_byte();
	NAND_CS_DISABLE();
	DBG("Register 0xC0: 0x%02X\n", stat_reg);
	
	NAND_CS_ENABLE();
	flash_write_byte(0x0F);
	flash_write_byte(0xD0); // Driver strength register
	drv_reg = flash_read_byte();
	NAND_CS_DISABLE();
	DBG("Register 0xD0: 0x%02X\n", drv_reg);
	
	// ID璇诲彇娴嬭瘯
	flash_ReadID(&mfg, &type, &dev, DEV_NAND);
	
	// 瀵勫瓨鍣ㄨ缁嗗垎鏋�
	DBG("Device ID Test: Mfg=0x%02X, Type=0x%02X, Dev=0x%02X\n", mfg, type, dev);
	DBG("NAND Status Register (0xC0): 0x%02X\n", stat_reg);
	DBG("Status Register: 0x%02X (BUSY=%d, E_FAIL=%d, P_FAIL=%d)\n", 
	    stat_reg, (stat_reg&0x01)?1:0, (stat_reg&0x04)?1:0, (stat_reg&0x08)?1:0);
	DBG("Protection Register (0xA0): 0x%02X\n", prot_reg);
	DBG("Configuration Register (0xB0): 0x%02X (ECC_EN=%d, BUF=%d)\n", 
	    conf_reg, (conf_reg&0x10)?1:0, (conf_reg&0x08)?1:0);
	
	// 瀹归噺妫�祴
	capacity = flash_GetTotalByte(DEV_NAND);
	DBG("NAND Flash ID: Manufacturer=0x%02X, MemoryType=0x%02X, DeviceID=0x%02X\n", mfg, type, dev);
	
	if(dev == 0x22) {
		DBG("Detected W25N02 NAND Flash (Winbond 2Gbit)\n");
	} else if(mfg == 0xEF) {
		DBG("Detected Winbond NAND Flash (Unknown model)\n");
	} else {
		DBG("Unknown NAND Flash device\n");
	}
	
	DBG("Total Capacity: %lu bytes (%.2f MB)\n", (unsigned long)capacity, capacity/1024.0/1024.0);
	
	// 鍧忓潡绠＄悊娴嬭瘯
	DBG("=== Bad Block Management Test ===\n");
	DBG("Total bad blocks detected: %d\n", bad_block_manager.table.count);
	if (bad_block_manager.table.count > 0) {
		DBG("Bad block list: ");
		for (i = 0; i < bad_block_manager.table.count; i++) {
			DBG("%d ", bad_block_manager.table.bad_blocks[i]);
		}
		DBG("\n");
	}
	
	// 绠�崟鐨勮鍐欐祴璇�
	DBG("Performing read/write test at address 0x%04lX...\n", test_address);
	
	// 鎿﹂櫎銆佸啓鍏ャ�璇诲彇
	flash_SectorErase(test_address, DEV_NAND);
	flash_PageProgram(test_address, test_data, 16, DEV_NAND);
	flash_ReadData(test_address, read_buffer, 16, DEV_NAND);
	
	// 姣旇緝鏁版嵁
	test_passed = true;
	for(i = 0; i < 16; i++) {
		if(test_data[i] != read_buffer[i]) {
			test_passed = false;
			break;
		}
	}
	
	if(test_passed) {
		DBG("Read/Write Test: PASSED\n");
	} else {
		DBG("Read/Write Test: FAILED\n");
		DBG("Expected: ");
		for(i = 0; i < 16; i++) DBG("%02X ", test_data[i]);
		DBG("\nActual:   ");
		for(i = 0; i < 16; i++) DBG("%02X ", read_buffer[i]);
		DBG("\n");
		
		// 妫�煡鏄惁鎵�湁璇诲彇鏁版嵁閮芥槸0x00
		all_zero = true;
		for(i = 0; i < 16; i++) {
			if(read_buffer[i] != 0x00) {
				all_zero = false;
				break;
			}
		}
		if(all_zero) {
			DBG("All read data is 0x00 - possible hardware issue or wrong commands\n");
		}
	}
	
	DBG("========== W25N02 Test Complete ==========\n");
}

// 鍧忓潡绠＄悊鍑芥暟瀹炵幇

// 鍒濆鍖栧潖鍧楃鐞嗗櫒
static void bad_block_manager_init(uint8_t dev) {
    if (bad_block_manager.initialized) {
        return;
    }
    
    uint8_t manufacturerID, memoryType, deviceID;
    flash_ReadID(&manufacturerID, &memoryType, &deviceID, dev);
    
    // 璁剧疆鍧忓潡琛ㄥ瓨鍌ㄥ湴鍧�紙鏀惧湪Flash寮�ご鐨勭涓�釜鍧楋紝璺宠繃绗�鍧楅伩鍏嶅啿绐侊級
    uint32_t total_size = flash_GetTotalByte(dev);
    
    // 瀵逛簬NAND Flash锛屽潖鍧楄〃鏀惧湪绗�鍧楋紙璺宠繃绗�鍧楋級
    if (dev == DEV_NAND) {
        bad_block_manager.table_address = 64 * 2048;  // 绗�鍧楃殑璧峰鍦板潃
    } else {
        // 瀵逛簬NOR Flash锛屾斁鍦ㄦ湯灏�
        bad_block_manager.table_address = total_size - sizeof(BadBlockTable);
    }
    
    DBG("Initializing bad block manager...\n");
    DBG("Total Flash size: 0x%08X bytes\n", total_size);
    DBG("Bad block table stored at: 0x%08X\n", bad_block_manager.table_address);
    
    // 涓存椂璁剧疆涓哄凡鍒濆鍖栵紝闃叉鍦ㄥ姞杞藉潖鍧楄〃鏃剁殑閫掑綊璋冪敤
    bad_block_manager.initialized = true;
    
    // 灏濊瘯鍔犺浇宸叉湁鐨勫潖鍧楄〃
    load_bad_block_table(dev);
    
    // 濡傛灉鍧忓潡琛ㄤ笉瀛樺湪鎴栨棤鏁堬紝鍒欏垱寤烘柊鐨�
    if (bad_block_manager.table.magic != BAD_BLOCK_TABLE_MAGIC || 
        bad_block_manager.table.version != BAD_BLOCK_TABLE_VERSION) {
        DBG("No valid bad block table found, creating new one\n");
        
        // 鍒濆鍖栨柊鐨勫潖鍧楄〃
        memset(&bad_block_manager.table, 0, sizeof(BadBlockTable));
        bad_block_manager.table.magic = BAD_BLOCK_TABLE_MAGIC;
        bad_block_manager.table.version = BAD_BLOCK_TABLE_VERSION;
        bad_block_manager.table.count = 0;
        
        // 鏆傛椂涓嶈繘琛屽叏鐩樻壂鎻忥紝鏀逛负杩愯鏃跺姩鎬佹娴嬪潖鍧�
        // 杩欐牱鍙互閬垮厤鍚姩鏃剁殑鏍堟孩鍑洪棶棰�
        DBG("Skipping initial bad block scan for now, will detect during runtime\n");
        
        // 淇濆瓨绌虹殑鍧忓潡琛�
        save_bad_block_table(dev);
    } else {
        DBG("Loaded existing bad block table, %d bad blocks found\n", bad_block_manager.table.count);
    }
    
    // 纭繚鍒濆鍖栨爣蹇楀凡璁剧疆
    bad_block_manager.initialized = true;
}

// 妫�煡鍧楁槸鍚︿负鍧忓潡
static bool is_block_bad(uint32_t block_address, uint8_t dev) {
    uint16_t i;
    
    if (!bad_block_manager.initialized) {
        bad_block_manager_init(dev);
    }
    
    // 妫�煡鍧楀湴鍧�槸鍚﹀湪鍧忓潡鍒楄〃涓�
    for (i = 0; i < bad_block_manager.table.count; i++) {
        if (bad_block_manager.table.bad_blocks[i] == block_address) {
            return true;
        }
    }
    return false;
}

// 灏嗗潡鏍囪涓哄潖鍧�
static bool mark_block_as_bad(uint32_t block_address, uint8_t dev) {
    if (!bad_block_manager.initialized) {
        bad_block_manager_init(dev);
    }
    
    // 妫�煡鏄惁宸茬粡鏄潖鍧�
    if (is_block_bad(block_address, dev)) {
        return true;
    }
    
    // 妫�煡鏄惁杩樻湁绌洪棿娣诲姞鏂扮殑鍧忓潡
    if (bad_block_manager.table.count >= sizeof(bad_block_manager.table.bad_blocks)/sizeof(bad_block_manager.table.bad_blocks[0])) {
        DBG("Cannot mark block %d as bad - bad block table is full\n", block_address);
        return false;
    }
    
    // 灏嗗潡娣诲姞鍒板潖鍧楀垪琛�
    bad_block_manager.table.bad_blocks[bad_block_manager.table.count++] = block_address;
    DBG("Block %d marked as bad\n", block_address);
    
    // 鍦ㄥ潡鐨勭涓�釜椤靛啓鍏ュ潖鍧楁爣璁�
    uint32_t address = block_to_address(block_address, dev);
    uint8_t marker = BAD_BLOCK_MARKER;
    flash_PageProgram(address, &marker, 1, dev);
    
    // 淇濆瓨鍧忓潡琛�
    save_bad_block_table(dev);
    return true;
}

// 鏌ユ壘涓嬩竴涓ソ鍧�
static uint32_t find_next_good_block(uint32_t start_block, uint8_t dev) {
    uint32_t total_blocks;
    uint32_t block;
    
    if (!bad_block_manager.initialized) {
        bad_block_manager_init(dev);
    }
    
    total_blocks = flash_GetTotalByte(dev) / (64 * 2048);
    
    for (block = start_block; block < total_blocks; block++) {
        if (!is_block_bad(block, dev)) {
            return block;
        }
    }
    
    // 濡傛灉鍦╯tart_block涔嬪悗娌℃湁鎵惧埌濂藉潡锛屼粠澶村紑濮嬫壘
    for (block = 0; block < start_block; block++) {
        if (!is_block_bad(block, dev)) {
            return block;
        }
    }
    
    // 鎵�湁鍧楅兘鏄潖鍧�
    return 0xFFFFFFFF; // 鏃犳晥鍧楀湴鍧�
}

// 淇濆瓨鍧忓潡琛ㄥ埌Flash
static void save_bad_block_table(uint8_t dev) {
    // 鎿﹂櫎鍧忓潡琛ㄦ墍鍦ㄧ殑鍧�
    flash_SectorErase(bad_block_manager.table_address, dev);
    
    // 鍐欏叆鍧忓潡琛�
    flash_PageProgram(bad_block_manager.table_address, 
                     (uint8_t*)&bad_block_manager.table, 
                     sizeof(BadBlockTable), 
                     dev);
                     
    DBG("Bad block table saved, %d bad blocks recorded\n", bad_block_manager.table.count);
}

// 浠嶧lash鍔犺浇鍧忓潡琛�
static void load_bad_block_table(uint8_t dev) {
    flash_ReadData(bad_block_manager.table_address,
                  (uint8_t*)&bad_block_manager.table,
                  sizeof(BadBlockTable),
                  dev);
}

// 灏嗗湴鍧�浆鎹负鍧楀彿
static uint32_t address_to_block(uint32_t address, uint8_t dev) {
    // W25N02姣忓潡64椤碉紝姣忛〉2048瀛楄妭
    return address / (64 * 2048);
}

// 灏嗗潡鍙疯浆鎹负鍦板潃
static uint32_t block_to_address(uint32_t block, uint8_t dev) {
    // W25N02姣忓潡64椤碉紝姣忛〉2048瀛楄妭
    return block * 64 * 2048;
}

// 鍏紑鐨勫潖鍧楃鐞嗘帴鍙ｅ嚱鏁�
uint8_t nand_check_bad_block(uint32_t block_address, uint8_t dev) {
    return is_block_bad(block_address, dev) ? 1 : 0;
}

uint8_t nand_mark_bad_block(uint32_t block_address, uint8_t dev) {
    return mark_block_as_bad(block_address, dev) ? 1 : 0;
}

uint32_t nand_find_next_good_block(uint32_t start_block, uint8_t dev) {
    return find_next_good_block(start_block, dev);
}

uint32_t nand_get_safe_write_address(uint32_t current_address, uint32_t bytes_to_write, uint8_t dev) {
    if (dev != DEV_NAND) {
        return current_address;  // NOR Flash涓嶉渶瑕佸潖鍧楃鐞�
    }
    
    uint32_t current_block = address_to_block(current_address, dev);
    
    // 妫�煡褰撳墠鍧楁槸鍚︿负鍧忓潡
    if (is_block_bad(current_block, dev)) {
        // 鎵惧埌涓嬩竴涓ソ鍧�
        uint32_t next_good_block = find_next_good_block(current_block + 1, dev);
        if (next_good_block == 0xFFFFFFFF) {
            DBG("ERROR: No good blocks available!\n");
            return 0xFFFFFFFF;
        }
        return block_to_address(next_good_block, dev);
    }
    
    return current_address;
}

// NAND Flash闊抽浼樺寲鍐欏叆 - 椤甸潰瀵归綈缂撳啿鏈哄埗
#define NAND_PAGE_SIZE 2048
#define NAND_AUDIO_BUFFER_SIZE NAND_PAGE_SIZE

static struct {
    uint8_t buffer[NAND_AUDIO_BUFFER_SIZE];
    uint32_t current_page_address;
    uint16_t buffer_pos;
    bool initialized;
} nand_audio_buffer = {{0}, 0, 0, false};

uint8_t nand_audio_write_buffered(uint32_t address, uint8_t* data, uint16_t size, uint8_t dev) {
    if (dev != DEV_NAND) {
        // NOR Flash鐩存帴鍐欏叆
        return flash_PageProgram(address, data, size, dev);
    }
    
    // 鍒濆鍖栫紦鍐插尯
    if (!nand_audio_buffer.initialized) {
        nand_audio_buffer.current_page_address = (address / NAND_PAGE_SIZE) * NAND_PAGE_SIZE;
        nand_audio_buffer.buffer_pos = address % NAND_PAGE_SIZE;
        nand_audio_buffer.initialized = true;
    }
    
    uint16_t data_pos = 0;
    
    while (data_pos < size) {
        uint32_t target_page = (address + data_pos) / NAND_PAGE_SIZE * NAND_PAGE_SIZE;
        
        // 濡傛灉璺ㄩ〉浜嗭紝鍏堝埛鏂板綋鍓嶇紦鍐插尯
        if (target_page != nand_audio_buffer.current_page_address) {
            if (nand_audio_buffer.buffer_pos > 0) {
                uint8_t result = nand_audio_flush_buffer(dev);
                if (result != FLASH_STATUS_OK) {
                    return result;
                }
            }
            
            // 鍒囨崲鍒版柊椤�
            nand_audio_buffer.current_page_address = target_page;
            nand_audio_buffer.buffer_pos = 0;
        }
        
        // 璁＄畻鍙互鍐欏叆褰撳墠椤电殑鏁版嵁閲�
        uint16_t remaining_in_page = NAND_PAGE_SIZE - nand_audio_buffer.buffer_pos;
        uint16_t remaining_data = size - data_pos;
        uint16_t bytes_to_copy = (remaining_in_page < remaining_data) ? remaining_in_page : remaining_data;
        
        // 澶嶅埗鏁版嵁鍒扮紦鍐插尯
        memcpy(&nand_audio_buffer.buffer[nand_audio_buffer.buffer_pos], 
               &data[data_pos], bytes_to_copy);
        
        nand_audio_buffer.buffer_pos += bytes_to_copy;
        data_pos += bytes_to_copy;
        
        // 濡傛灉椤甸潰缂撳啿鍖烘弧浜嗭紝鍒锋柊鍒癋lash
        if (nand_audio_buffer.buffer_pos >= NAND_PAGE_SIZE) {
            uint8_t result = nand_audio_flush_buffer(dev);
            if (result != FLASH_STATUS_OK) {
                return result;
            }
        }
    }
    
    return FLASH_STATUS_OK;
}

// 鍒濆鍖朜AND鏅鸿兘闊抽缂撳啿绯荤粺
void nand_smart_audio_init(void) {
    nand_audio_buffer.buffer_pos = 0;
    nand_audio_buffer.current_page_address = 0;
    nand_audio_buffer.initialized = true;
    DBG("NAND smart audio buffer initialized\n");
}

// 鑾峰彇褰撳墠NAND闊抽鍐欏叆鍦板潃
uint32_t nand_smart_audio_get_address(void) {
    if (!nand_audio_buffer.initialized) {
        return 0;
    }
    return nand_audio_buffer.current_page_address + nand_audio_buffer.buffer_pos;
}

uint8_t nand_audio_flush_buffer(uint8_t dev) {
    if (!nand_audio_buffer.initialized || nand_audio_buffer.buffer_pos == 0) {
        return FLASH_STATUS_OK;
    }
    
    // 妫�煡鐩爣鍧楁槸鍚︿负鍧忓潡
    uint32_t block = address_to_block(nand_audio_buffer.current_page_address, dev);
    if (is_block_bad(block, dev)) {
        // 鎵惧埌涓嬩竴涓ソ鍧�
        uint32_t next_good_block = find_next_good_block(block + 1, dev);
        if (next_good_block == 0xFFFFFFFF) {
            DBG("ERROR: No good blocks available for audio buffer flush!\n");
            return FLASH_STATUS_ERROR;
        }
        nand_audio_buffer.current_page_address = block_to_address(next_good_block, dev);
    }
    
    // 灏嗙紦鍐插尯鍐呭鍐欏叆Flash锛堥〉闈㈠榻愶級
    uint8_t result = flash_PageProgram(nand_audio_buffer.current_page_address, 
                                      nand_audio_buffer.buffer, 
                                      nand_audio_buffer.buffer_pos, 
                                      dev);
    
    if (result == FLASH_STATUS_OK) {
        // 閲嶇疆缂撳啿鍖�
        nand_audio_buffer.buffer_pos = 0;
        nand_audio_buffer.current_page_address += NAND_PAGE_SIZE;
        return FLASH_STATUS_OK;
    } else {
        // 鍐欏叆澶辫触锛屾爣璁板潖鍧楀苟閲嶈瘯
        mark_block_as_bad(block, dev);
        DBG("Audio buffer flush failed, marked block %lu as bad\n", (unsigned long)block);
        return FLASH_STATUS_ERROR;
    }
}

// NAND鏅鸿兘闊抽鍐欏叆
uint8_t nand_smart_audio_write(uint32_t address, uint8_t* data, uint16_t size, uint8_t dev) {
    // 鐩存帴浣跨敤鐜版湁鐨勭紦鍐插啓鍏ュ姛鑳�
    return nand_audio_write_buffered(address, data, size, dev);
}

// NAND鏅鸿兘闊抽璇诲彇
uint8_t nand_smart_audio_read(uint32_t address, uint8_t* data, uint16_t size, uint8_t dev) {
    // 瀵逛簬璇诲彇锛岀洿鎺ヤ娇鐢ㄦ爣鍑咶lash璇诲彇
    // TODO: 濡傛灉闇�锛屽彲浠ユ坊鍔犳櫤鑳借鍙栭�杈戯紙鑰冭檻椤甸潰鏄犲皠绛夛級
    BG_flash_manager.ReadData(address, data, size, dev);
    return FLASH_STATUS_OK;
}

// NAND鏅鸿兘闊抽鍒锋柊
uint8_t nand_smart_audio_flush(uint8_t dev) {
    return nand_audio_flush_buffer(dev);
}
