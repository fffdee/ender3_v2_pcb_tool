#ifndef BANUX_INTERNAL_FLASH_DISKIO_H
#define BANUX_INTERNAL_FLASH_DISKIO_H

#include "ff_gen_drv.h"

#define BANUX_FLASH_DISK_BASE          0x0804D000u
#define BANUX_FLASH_DISK_SIZE          (200u * 1024u)
#define BANUX_FLASH_DISK_SECTOR_SIZE   512u
#define BANUX_FLASH_DISK_SECTOR_COUNT  400u

/* 每簇扇区数。
 * 400 扇区若按每簇 1 扇区算，数据区约 396 簇，FAT12 需要 396*1.5 = 594 字节，
 * 超过 1 个 FAT 扇区(512 字节)的容量，FAT 会写溢出。
 * 取每簇 2 扇区(1KB 簇)：约 198 簇，FAT 仅需约 297 字节，安全落在 1 个扇区内，
 * 同时 198 簇 < 4085，仍然是 FAT12，与引导扇区的 "FAT12   " 标识一致。 */
#define BANUX_FLASH_DISK_CLUSTER_SECTORS 2u

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
