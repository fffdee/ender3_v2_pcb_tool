#include <stdio.h>
#include <string.h>
#include "drv_eeprom.h"
#include "drv_device.h"
#include "debug.h"
#include "stm32f1xx_hal.h"

#define EEPROM_SCL_PORT       GPIOA
#define EEPROM_SCL_PIN        GPIO_PIN_12
#define EEPROM_SDA_PORT       GPIOA
#define EEPROM_SDA_PIN        GPIO_PIN_11
#define EEPROM_I2C_BASE_ADDR  0x50u
#define EEPROM_PAGE_SIZE      16u
#define EEPROM_BLOCK_SIZE     256u
#define EEPROM_I2C_DELAY_US   5u
#define EEPROM_READY_MS       10u

typedef struct {
    uint16_t address;
    uint8_t detected;
} EepromPriv_t;

static EepromPriv_t s_eeprom;

static void eeprom_delay_us(uint32_t us)
{
    uint32_t start;
    uint32_t cycles = (SystemCoreClock / 1000000u) * us;

    start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < cycles) {
    }
}

static void eeprom_scl(int high)
{
    HAL_GPIO_WritePin(EEPROM_SCL_PORT, EEPROM_SCL_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void eeprom_sda(int high)
{
    HAL_GPIO_WritePin(EEPROM_SDA_PORT, EEPROM_SDA_PIN,
                      high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static int eeprom_sda_read(void)
{
    return HAL_GPIO_ReadPin(EEPROM_SDA_PORT, EEPROM_SDA_PIN) == GPIO_PIN_SET;
}

static void eeprom_i2c_start(void)
{
    eeprom_sda(1);
    eeprom_scl(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_sda(0);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_scl(0);
}

static void eeprom_i2c_stop(void)
{
    eeprom_sda(0);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_scl(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_sda(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
}

static int eeprom_i2c_write_byte(uint8_t value)
{
    uint8_t mask;
    int ack;

    for (mask = 0x80u; mask != 0u; mask >>= 1) {
        eeprom_sda((value & mask) != 0u);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
        eeprom_scl(1);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
        eeprom_scl(0);
    }
    eeprom_sda(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_scl(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    ack = !eeprom_sda_read();
    eeprom_scl(0);
    return ack ? 0 : -1;
}

static uint8_t eeprom_i2c_read_byte(int acknowledge)
{
    uint8_t value = 0;
    uint8_t i;

    eeprom_sda(1);
    for (i = 0; i < 8u; i++) {
        value <<= 1;
        eeprom_scl(1);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
        if (eeprom_sda_read()) value |= 1u;
        eeprom_scl(0);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
    }
    eeprom_sda(acknowledge ? 0 : 1);
    eeprom_scl(1);
    eeprom_delay_us(EEPROM_I2C_DELAY_US);
    eeprom_scl(0);
    eeprom_sda(1);
    return value;
}

static uint8_t eeprom_device_address(uint16_t address)
{
    return (uint8_t)(EEPROM_I2C_BASE_ADDR | ((address >> 8) & 0x07u));
}

static int eeprom_probe(uint8_t deviceAddress)
{
    int ret;

    eeprom_i2c_start();
    ret = eeprom_i2c_write_byte((uint8_t)(deviceAddress << 1));
    eeprom_i2c_stop();
    return ret;
}

static int eeprom_wait_ready(uint8_t deviceAddress)
{
    uint32_t start = HAL_GetTick();

    do {
        if (eeprom_probe(deviceAddress) == 0) return 0;
        HAL_Delay(1u);
    } while ((HAL_GetTick() - start) < EEPROM_READY_MS);
    return -1;
}

static void eeprom_bus_init(void)
{
    GPIO_InitTypeDef init;
    uint8_t i;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, EEPROM_SCL_PIN | EEPROM_SDA_PIN, GPIO_PIN_SET);
    init.Pin = EEPROM_SCL_PIN | EEPROM_SDA_PIN;
    init.Mode = GPIO_MODE_OUTPUT_OD;
    init.Pull = GPIO_NOPULL;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &init);

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    eeprom_sda(1);
    for (i = 0; i < 9u && !eeprom_sda_read(); i++) {
        eeprom_scl(0);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
        eeprom_scl(1);
        eeprom_delay_us(EEPROM_I2C_DELAY_US);
    }
    eeprom_i2c_stop();
}

int DrvEeprom_Read(uint16_t address, uint8_t *data, uint16_t len)
{
    uint16_t done = 0;

    if (!data || address > DRV_EEPROM_SIZE || len > DRV_EEPROM_SIZE - address) {
        return -1;
    }
    while (done < len) {
        uint16_t current = (uint16_t)(address + done);
        uint16_t chunk = (uint16_t)(EEPROM_BLOCK_SIZE - (current & 0xFFu));
        uint16_t i;
        uint8_t device = eeprom_device_address(current);

        if (chunk > (uint16_t)(len - done)) chunk = (uint16_t)(len - done);
        eeprom_i2c_start();
        if (eeprom_i2c_write_byte((uint8_t)(device << 1)) != 0 ||
            eeprom_i2c_write_byte((uint8_t)current) != 0) {
            eeprom_i2c_stop();
            return -1;
        }
        eeprom_i2c_start();
        if (eeprom_i2c_write_byte((uint8_t)((device << 1) | 1u)) != 0) {
            eeprom_i2c_stop();
            return -1;
        }
        for (i = 0; i < chunk; i++) {
            data[done + i] = eeprom_i2c_read_byte(i + 1u < chunk);
        }
        eeprom_i2c_stop();
        done = (uint16_t)(done + chunk);
    }
    return (int)len;
}

int DrvEeprom_Write(uint16_t address, const uint8_t *data, uint16_t len)
{
    uint16_t done = 0;

    if (!data || len == 0u || address > DRV_EEPROM_SIZE ||
        len > DRV_EEPROM_SIZE - address) {
        return -1;
    }
    while (done < len) {
        uint16_t current = (uint16_t)(address + done);
        uint16_t chunk = (uint16_t)(EEPROM_PAGE_SIZE - (current & (EEPROM_PAGE_SIZE - 1u)));
        uint16_t blockRemain = (uint16_t)(EEPROM_BLOCK_SIZE - (current & 0xFFu));
        uint16_t i;
        uint8_t device = eeprom_device_address(current);

        if (chunk > blockRemain) chunk = blockRemain;
        if (chunk > (uint16_t)(len - done)) chunk = (uint16_t)(len - done);
        eeprom_i2c_start();
        if (eeprom_i2c_write_byte((uint8_t)(device << 1)) != 0 ||
            eeprom_i2c_write_byte((uint8_t)current) != 0) {
            eeprom_i2c_stop();
            return -1;
        }
        for (i = 0; i < chunk; i++) {
            if (eeprom_i2c_write_byte(data[done + i]) != 0) {
                eeprom_i2c_stop();
                return -1;
            }
        }
        eeprom_i2c_stop();
        if (eeprom_wait_ready(device) != 0) return -1;
        done = (uint16_t)(done + chunk);
    }
    return (int)len;
}

int DrvEeprom_Erase(void)
{
    uint8_t erased[EEPROM_PAGE_SIZE];
    uint16_t address;

    memset(erased, 0xFF, sizeof(erased));
    for (address = 0; address < DRV_EEPROM_SIZE; address += EEPROM_PAGE_SIZE) {
        if (DrvEeprom_Write(address, erased, sizeof(erased)) < 0) return -1;
    }
    return 0;
}

static int eeprom_drv_init(void *priv)
{
    EepromPriv_t *state = (EepromPriv_t *)priv;

    memset(state, 0, sizeof(*state));
    eeprom_bus_init();
    state->detected = (eeprom_probe(EEPROM_I2C_BASE_ADDR) == 0) ? 1u : 0u;
    if (!state->detected) {
        DBG("[EEPROM] BL24C16A not detected on PA12(SCL)/PA11(SDA)\n");
        return -1;
    }
    DBG("[EEPROM] BL24C16A ready, size=%u, SCL=PA12 SDA=PA11\n",
        DRV_EEPROM_SIZE);
    return 0;
}

static int eeprom_drv_read(void *priv, uint8_t *buf, uint32_t len)
{
    EepromPriv_t *state = (EepromPriv_t *)priv;
    if (len > 0xFFFFu) return -1;
    return DrvEeprom_Read(state->address, buf, (uint16_t)len);
}

static int eeprom_drv_write(void *priv, const uint8_t *buf, uint32_t len)
{
    EepromPriv_t *state = (EepromPriv_t *)priv;
    if (len > 0xFFFFu) return -1;
    return DrvEeprom_Write(state->address, buf, (uint16_t)len);
}

static int get_type(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "BL24C16A");
}

static int get_size(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", DRV_EEPROM_SIZE);
}

static int get_page_size(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "%u", EEPROM_PAGE_SIZE);
}

static int get_address(char *buf, uint16_t maxLen, void *userData)
{
    EepromPriv_t *state = (EepromPriv_t *)userData;
    return snprintf(buf, maxLen, "%u", state->address);
}

static int set_address(const char *value, void *userData)
{
    EepromPriv_t *state = (EepromPriv_t *)userData;
    unsigned long address;
    if (!value || sscanf(value, "%lu", &address) != 1 || address >= DRV_EEPROM_SIZE) return -1;
    state->address = (uint16_t)address;
    return 0;
}

static int get_detected(char *buf, uint16_t maxLen, void *userData)
{
    EepromPriv_t *state = (EepromPriv_t *)userData;
    return snprintf(buf, maxLen, "%u", state->detected);
}

static int get_value(char *buf, uint16_t maxLen, void *userData)
{
    EepromPriv_t *state = (EepromPriv_t *)userData;
    uint8_t value;
    if (DrvEeprom_Read(state->address, &value, 1u) != 1) return -1;
    return snprintf(buf, maxLen, "%u", value);
}

static int set_value(const char *text, void *userData)
{
    EepromPriv_t *state = (EepromPriv_t *)userData;
    unsigned long value;
    uint8_t byte;
    if (!text || sscanf(text, "%lu", &value) != 1 || value > 255u) return -1;
    byte = (uint8_t)value;
    return (DrvEeprom_Write(state->address, &byte, 1u) == 1) ? 0 : -1;
}

static int get_erase(char *buf, uint16_t maxLen, void *userData)
{
    (void)userData;
    return snprintf(buf, maxLen, "0");
}

static int set_erase(const char *value, void *userData)
{
    (void)userData;
    return (value && strcmp(value, "1") == 0) ? DrvEeprom_Erase() : -1;
}

static const FsParamDef_t eeprom_params[] = {
    FS_PARAM_DEF("type",      "EEPROM model",                  get_type,      NULL),
    FS_PARAM_DEF("size",      "capacity in bytes",             get_size,      NULL),
    FS_PARAM_DEF("page_size", "write page size in bytes",      get_page_size, NULL),
    FS_PARAM_DEF("address",   "read/write start address",       get_address,   set_address),
    FS_PARAM_DEF("value",     "byte at current address",        get_value,     set_value),
    FS_PARAM_DEF("detected",  "device acknowledged (0/1)",      get_detected,  NULL),
    FS_PARAM_DEF("erase",     "write 1 to fill EEPROM with FF", get_erase,     set_erase),
    FS_PARAM_END
};

static DrvDevice_t eeprom_drv = {
    .name = "eeprom",
    .desc = "BL24C16A 16-Kbit software-I2C EEPROM",
    .bus = DRV_BUS_I2C,
    .init = eeprom_drv_init,
    .deinit = NULL,
    .open = NULL,
    .close = NULL,
    .read = eeprom_drv_read,
    .write = eeprom_drv_write,
    .ioctl = NULL,
    .params = eeprom_params,
    .privData = &s_eeprom,
};

int DrvEeprom_Register(void)
{
    return DrvDevice_Register(&eeprom_drv);
}
