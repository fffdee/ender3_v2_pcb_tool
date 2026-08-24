#include "bl_flash.h"
#include "stm32f1xx_hal.h"

static void cfg_defaults(bl_cfg_t *cfg)
{
    cfg->cfg_magic = BL_MAGIC_CFG;
    cfg->seq = 1;
    cfg->boot_mode = 0;
    cfg->active_part = 0;
    cfg->boot_fail_cnt = 0;
    cfg->reserved = 0;
    cfg->pending = 0;
    cfg->expected_size = 0;
    cfg->enter_boot = 0;
}

static int cfg_valid(const bl_cfg_t *cfg)
{
    uint32_t i;
    const uint8_t *p = (const uint8_t *)cfg;

    for (i = 0; i < sizeof(bl_cfg_t); i++) {
        if (p[i] != 0xFFu) {
            return cfg->cfg_magic == BL_MAGIC_CFG;
        }
    }
    return 0;
}

static int flash_erase_page(uint32_t addr)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = addr;
    erase.NbPages = 1;
    return (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK) ? 0 : -1;
}

static int flash_program_buf(uint32_t addr, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t hw;

    if ((addr & 1u) != 0u) {
        return -1;
    }

    for (i = 0; i < len; i += 2u) {
        hw = data[i];
        if ((i + 1u) < len) {
            hw |= ((uint16_t)data[i + 1u]) << 8;
        } else {
            hw |= 0xFF00u;
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, hw) != HAL_OK) {
            return -1;
        }
    }
    return 0;
}

void bl_cfg_load(bl_cfg_t *cfg)
{
    const bl_cfg_t *p0 = (const bl_cfg_t *)BL_CFG_PAGE0;
    const bl_cfg_t *p1 = (const bl_cfg_t *)BL_CFG_PAGE1;
    int v0 = cfg_valid(p0);
    int v1 = cfg_valid(p1);

    if (v0 && v1) {
        *cfg = (p0->seq >= p1->seq) ? *p0 : *p1;
    } else if (v0) {
        *cfg = *p0;
    } else if (v1) {
        *cfg = *p1;
    } else {
        cfg_defaults(cfg);
    }
}

int bl_cfg_save(const bl_cfg_t *cfg)
{
    const bl_cfg_t *p0 = (const bl_cfg_t *)BL_CFG_PAGE0;
    const bl_cfg_t *p1 = (const bl_cfg_t *)BL_CFG_PAGE1;
    bl_cfg_t tmp = *cfg;
    uint32_t dst;
    uint32_t old;
    int v0 = cfg_valid(p0);
    int v1 = cfg_valid(p1);

    tmp.cfg_magic = BL_MAGIC_CFG;
    if (v0 && v1) {
        if (p0->seq >= p1->seq) {
            tmp.seq = p0->seq + 1u;
            dst = BL_CFG_PAGE1;
            old = BL_CFG_PAGE0;
        } else {
            tmp.seq = p1->seq + 1u;
            dst = BL_CFG_PAGE0;
            old = BL_CFG_PAGE1;
        }
    } else if (v0) {
        tmp.seq = p0->seq + 1u;
        dst = BL_CFG_PAGE1;
        old = BL_CFG_PAGE0;
    } else if (v1) {
        tmp.seq = p1->seq + 1u;
        dst = BL_CFG_PAGE0;
        old = BL_CFG_PAGE1;
    } else {
        tmp.seq = 1;
        dst = BL_CFG_PAGE0;
        old = 0;
    }

    HAL_FLASH_Unlock();
    if (flash_erase_page(dst) != 0) {
        HAL_FLASH_Lock();
        return -1;
    }
    if (flash_program_buf(dst, (const uint8_t *)&tmp, sizeof(tmp)) != 0) {
        HAL_FLASH_Lock();
        return -1;
    }
    if (old != 0u) {
        (void)flash_erase_page(old);
    }
    HAL_FLASH_Lock();
    return 0;
}

int bl_cfg_set_pending(int pending, uint32_t expected_size)
{
    bl_cfg_t cfg;

    bl_cfg_load(&cfg);
    cfg.pending = pending ? BL_MAGIC_PEND : 0u;
    cfg.expected_size = expected_size;
    return bl_cfg_save(&cfg);
}

int bl_cfg_set_enter_boot(int on)
{
    bl_cfg_t cfg;

    bl_cfg_load(&cfg);
    cfg.enter_boot = on ? BL_MAGIC_ENTER_BOOT : 0u;
    return bl_cfg_save(&cfg);
}

int bl_flash_erase_app(void)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0;
    int ok;

    HAL_FLASH_Unlock();
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BL_APP_BASE;
    erase.NbPages = BL_APP_SIZE / BL_FLASH_PAGE_SIZE;
    ok = (HAL_FLASHEx_Erase(&erase, &page_err) == HAL_OK) ? 0 : -1;
    HAL_FLASH_Lock();
    return ok;
}

int bl_flash_write(uint32_t offset, const uint8_t *data, uint32_t len)
{
    int ok;

    if ((offset + len) > BL_APP_SIZE) {
        return -1;
    }
    HAL_FLASH_Unlock();
    ok = flash_program_buf(BL_APP_BASE + offset, data, len);
    HAL_FLASH_Lock();
    return ok;
}

int bl_app_vector_valid(void)
{
    uint32_t sp = *(volatile uint32_t *)BL_APP_BASE;
    uint32_t pc = *(volatile uint32_t *)(BL_APP_BASE + 4u);

    if ((sp < BL_SRAM_BASE) || (sp > (BL_SRAM_BASE + BL_SRAM_SIZE))) {
        return 0;
    }
    if ((pc < BL_APP_BASE) || (pc >= 0x08080000u) || ((pc & 1u) == 0u)) {
        return 0;
    }
    return 1;
}

void bl_jump_to_app(void)
{
    uint32_t sp = *(volatile uint32_t *)BL_APP_BASE;
    uint32_t pc = *(volatile uint32_t *)(BL_APP_BASE + 4u);
    void (*app)(void) = (void (*)(void))pc;

    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    HAL_DeInit();
    SCB->VTOR = BL_APP_BASE;
    __set_MSP(sp);
    app();
    while (1) {
    }
}
