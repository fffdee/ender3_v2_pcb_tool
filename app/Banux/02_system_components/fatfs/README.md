# Banux FatFs component

This directory owns the complete FatFs integration used by Banux:

- `app/`: Cube-generated filesystem entry and volume objects.
- `target/`: STM32F1 SDIO BSP and `ffconf.h`.
- `src/`: Elm-Chan FatFs core, ST diskio glue and optional modules.

The SD hardware driver remains in `01_driver/sd`. It mounts this component and
exposes files through the Banux VFS. Do not add another FatFs/FAT32 copy to the
project.

STM32CubeMX normally regenerates FatFs under `app/FATFS`; after regenerating
the project, merge generated changes into this component instead of linking a
second copy.
