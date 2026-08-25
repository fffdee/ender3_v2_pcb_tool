#ifndef __DRV_EEPROM_H__
#define __DRV_EEPROM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

#define DRV_EEPROM_SIZE  2048u

int DrvEeprom_Register(void);
int DrvEeprom_Read(uint16_t address, uint8_t *data, uint16_t len);
int DrvEeprom_Write(uint16_t address, const uint8_t *data, uint16_t len);
int DrvEeprom_Erase(void);

#ifdef __cplusplus
}
#endif

#endif /* __DRV_EEPROM_H__ */
