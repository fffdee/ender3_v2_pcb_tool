#ifndef BL_UPDATE_H
#define BL_UPDATE_H

#include <stdint.h>

void bl_update_init(void);
int  bl_sd_present(void);
int  bl_sd_try_upgrade(void);

int  bl_upg_start(uint32_t total);
int  bl_upg_data(uint32_t offset, const uint8_t *data, uint32_t len);
int  bl_upg_finish(uint32_t total);
int  bl_upg_erase(void);
int  bl_upg_can_jump(void);
void bl_upg_on_jump_ok(void);

uint8_t  bl_upg_boot_fail_cnt(void);
uint32_t bl_upg_written(void);

#endif /* BL_UPDATE_H */
