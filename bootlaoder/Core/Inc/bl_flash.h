#ifndef BL_FLASH_H
#define BL_FLASH_H

#include "bl_config.h"

void bl_cfg_load(bl_cfg_t *cfg);
int  bl_cfg_save(const bl_cfg_t *cfg);
int  bl_cfg_set_pending(int pending, uint32_t expected_size);
int  bl_cfg_set_enter_boot(int on);

int  bl_flash_erase_app(void);
int  bl_flash_write(uint32_t offset, const uint8_t *data, uint32_t len);

int  bl_app_vector_valid(void);
void bl_jump_to_app(void);

#endif /* BL_FLASH_H */
