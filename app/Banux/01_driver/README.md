# Platform drivers

This directory contains concrete hardware drivers and board registration code.
Each driver depends on the portable device interfaces in
`02_system_components/driver_framework`; the framework does not depend on any
file in this directory.

- `driver_init.c/.h`: Ender-3 V2 driver registration order and failure policy.
- `eeprom/`: BL24C16A software-I2C EEPROM.
- `sd/`: SDIO device and FatFs-to-VFS mount adapter.
- `stepper/`: X/Y/Z/E stepper and X/Y/Z limit inputs.
- `timer/`: SysTick-backed 1 ms timer device.
- `uart/`: UART1/UART3 device adapters.
- `library/`: inactive reusable and legacy hardware drivers.
