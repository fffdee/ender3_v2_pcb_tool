#ifndef BANUX_INTERNAL_FLASH_DISKIO_H
#define BANUX_INTERNAL_FLASH_DISKIO_H

#include "ff_gen_drv.h"

#define BANUX_FLASH_DISK_BASE          0x0807A000u
#define BANUX_FLASH_DISK_SIZE          (20u * 1024u)
#define BANUX_FLASH_DISK_SECTOR_SIZE   512u
#define BANUX_FLASH_DISK_SECTOR_COUNT  40u

#if (BANUX_FLASH_DISK_SIZE != \
     (BANUX_FLASH_DISK_SECTOR_SIZE * BANUX_FLASH_DISK_SECTOR_COUNT))
#error "Internal Flash disk geometry is inconsistent"
#endif
#if ((BANUX_FLASH_DISK_BASE & 0x7FFu) != 0u)
#error "Internal Flash disk must start on a 2 KB STM32F103RE page"
#endif
#if ((BANUX_FLASH_DISK_BASE + BANUX_FLASH_DISK_SIZE) > 0x0807F000u)
#error "Internal Flash disk overlaps bootloader configuration pages"
#endif

extern const Diskio_drvTypeDef InternalFlash_Driver;

#endif /* BANUX_INTERNAL_FLASH_DISKIO_H */
