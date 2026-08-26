#include <string.h>
#include "internal_flash_diskio.h"
#include "stm32f1xx_hal.h"

#define BANUX_FLASH_PAGE_SIZE  0x800u
#define SECTORS_PER_PAGE      (BANUX_FLASH_PAGE_SIZE / BANUX_FLASH_DISK_SECTOR_SIZE)

static uint8_t s_pageBuffer[BANUX_FLASH_PAGE_SIZE];
static DSTATUS s_status = STA_NOINIT;

static void put_word(uint8_t *buffer, uint16_t offset, uint16_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8);
}

static void put_dword(uint8_t *buffer, uint16_t offset, uint32_t value)
{
    put_word(buffer, offset, (uint16_t)value);
    put_word(buffer, offset + 2u, (uint16_t)(value >> 16));
}

static int program_page(uint32_t address, const uint8_t *data)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t pageError = 0u;
    uint32_t offset;
    uint32_t primask = __get_PRIMASK();
    int result = -1;

    __disable_irq();
    if (HAL_FLASH_Unlock() != HAL_OK) goto done;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = address;
    erase.NbPages = 1u;
    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) goto lock;

    for (offset = 0u; offset < BANUX_FLASH_PAGE_SIZE; offset += 2u) {
        uint16_t value = (uint16_t)data[offset] |
                         ((uint16_t)data[offset + 1u] << 8);
        if (value != 0xFFFFu &&
            HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                              address + offset, value) != HAL_OK) {
            goto lock;
        }
    }
    result = memcmp((const void *)address, data, BANUX_FLASH_PAGE_SIZE) == 0 ? 0 : -1;

lock:
    HAL_FLASH_Lock();
done:
    if (!primask) __enable_irq();
    return result;
}

static int format_if_needed(void)
{
    const uint8_t *boot = (const uint8_t *)BANUX_FLASH_DISK_BASE;
    uint32_t address;
    FLASH_EraseInitTypeDef erase;
    uint32_t pageError = 0u;
    uint32_t primask;

    if (boot[510] == 0x55u && boot[511] == 0xAAu &&
        boot[11] == 0x00u && boot[12] == 0x02u &&
        boot[19] == BANUX_FLASH_DISK_SECTOR_COUNT && boot[20] == 0u &&
        boot[512] == 0xF8u && boot[513] == 0xFFu && boot[514] == 0xFFu) {
        return 0;
    }

    memset(s_pageBuffer, 0, sizeof(s_pageBuffer));
    s_pageBuffer[0] = 0xEBu;
    s_pageBuffer[1] = 0x3Cu;
    s_pageBuffer[2] = 0x90u;
    memcpy(&s_pageBuffer[3], "BANUX1.0", 8u);
    put_word(s_pageBuffer, 11u, BANUX_FLASH_DISK_SECTOR_SIZE);
    s_pageBuffer[13] = 1u;       /* sectors per cluster */
    put_word(s_pageBuffer, 14u, 1u);
    s_pageBuffer[16] = 1u;       /* one FAT */
    put_word(s_pageBuffer, 17u, 32u);
    put_word(s_pageBuffer, 19u, BANUX_FLASH_DISK_SECTOR_COUNT);
    s_pageBuffer[21] = 0xF8u;
    put_word(s_pageBuffer, 22u, 1u);
    put_word(s_pageBuffer, 24u, 1u);
    put_word(s_pageBuffer, 26u, 1u);
    s_pageBuffer[36] = 0x80u;
    s_pageBuffer[38] = 0x29u;
    put_dword(s_pageBuffer, 39u, 0x584E4142u);
    memcpy(&s_pageBuffer[43], "BANUXFLASH ", 11u);
    memcpy(&s_pageBuffer[54], "FAT12   ", 8u);
    s_pageBuffer[510] = 0x55u;
    s_pageBuffer[511] = 0xAAu;
    s_pageBuffer[512] = 0xF8u;
    s_pageBuffer[513] = 0xFFu;
    s_pageBuffer[514] = 0xFFu;

    if (program_page(BANUX_FLASH_DISK_BASE, s_pageBuffer) != 0) return -1;

    /* Remaining pages contain data clusters and start erased. */
    primask = __get_PRIMASK();
    __disable_irq();
    if (HAL_FLASH_Unlock() != HAL_OK) {
        if (!primask) __enable_irq();
        return -1;
    }
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BANUX_FLASH_DISK_BASE + BANUX_FLASH_PAGE_SIZE;
    erase.NbPages = (BANUX_FLASH_DISK_SIZE / BANUX_FLASH_PAGE_SIZE) - 1u;
    if (HAL_FLASHEx_Erase(&erase, &pageError) != HAL_OK) {
        HAL_FLASH_Lock();
        if (!primask) __enable_irq();
        return -1;
    }
    HAL_FLASH_Lock();
    if (!primask) __enable_irq();

    for (address = BANUX_FLASH_DISK_BASE + BANUX_FLASH_PAGE_SIZE;
         address < BANUX_FLASH_DISK_BASE + BANUX_FLASH_DISK_SIZE;
         address += BANUX_FLASH_PAGE_SIZE) {
        if (*(const uint32_t *)address != 0xFFFFFFFFu) return -1;
    }
    return 0;
}

static DSTATUS flash_initialize(BYTE lun)
{
    (void)lun;
    s_status = format_if_needed() == 0 ? 0u : STA_NOINIT;
    return s_status;
}

static DSTATUS flash_status(BYTE lun)
{
    (void)lun;
    return s_status;
}

static DRESULT flash_read(BYTE lun, BYTE *buffer, DWORD sector, UINT count)
{
    (void)lun;
    if (!buffer || count == 0u || sector + count > BANUX_FLASH_DISK_SECTOR_COUNT)
        return RES_PARERR;
    memcpy(buffer, (const void *)(BANUX_FLASH_DISK_BASE +
           sector * BANUX_FLASH_DISK_SECTOR_SIZE),
           count * BANUX_FLASH_DISK_SECTOR_SIZE);
    return RES_OK;
}

static DRESULT flash_write(BYTE lun, const BYTE *buffer, DWORD sector, UINT count)
{
    (void)lun;
    if (!buffer || count == 0u || sector + count > BANUX_FLASH_DISK_SECTOR_COUNT)
        return RES_PARERR;

    while (count > 0u) {
        uint32_t page = sector / SECTORS_PER_PAGE;
        uint32_t pageSector = sector % SECTORS_PER_PAGE;
        uint32_t chunk = SECTORS_PER_PAGE - pageSector;
        uint32_t pageAddress = BANUX_FLASH_DISK_BASE + page * BANUX_FLASH_PAGE_SIZE;
        if (chunk > count) chunk = count;

        memcpy(s_pageBuffer, (const void *)pageAddress, BANUX_FLASH_PAGE_SIZE);
        memcpy(&s_pageBuffer[pageSector * BANUX_FLASH_DISK_SECTOR_SIZE],
               buffer, chunk * BANUX_FLASH_DISK_SECTOR_SIZE);
        if (memcmp((const void *)pageAddress, s_pageBuffer, BANUX_FLASH_PAGE_SIZE) != 0 &&
            program_page(pageAddress, s_pageBuffer) != 0) {
            return RES_ERROR;
        }
        buffer += chunk * BANUX_FLASH_DISK_SECTOR_SIZE;
        sector += chunk;
        count -= chunk;
    }
    return RES_OK;
}

static DRESULT flash_ioctl(BYTE lun, BYTE command, void *buffer)
{
    (void)lun;
    if (s_status & STA_NOINIT) return RES_NOTRDY;
    switch (command) {
        case CTRL_SYNC: return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD *)buffer = BANUX_FLASH_DISK_SECTOR_COUNT;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buffer = BANUX_FLASH_DISK_SECTOR_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD *)buffer = SECTORS_PER_PAGE;
            return RES_OK;
        default: return RES_PARERR;
    }
}

const Diskio_drvTypeDef InternalFlash_Driver = {
    flash_initialize, flash_status, flash_read, flash_write, flash_ioctl
};
