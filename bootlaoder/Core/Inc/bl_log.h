#ifndef BL_LOG_H
#define BL_LOG_H

#include <stdint.h>

void bl_log(const char *s);
void bl_log_u32(const char *prefix, uint32_t value);

#endif /* BL_LOG_H */
