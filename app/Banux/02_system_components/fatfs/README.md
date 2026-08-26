# Banux FatFs component

This directory owns the complete FatFs integration used by Banux:

- `app/`: Cube-generated filesystem entry and volume objects.
- `target/`: STM32F1 SDIO BSP and `ffconf.h`.
- `src/`: Elm-Chan FatFs core, ST diskio glue and optional modules.

The SD hardware driver remains in `01_driver/sd`. It mounts this component and
exposes files through the Banux VFS. Do not add another FatFs/FAT32 copy to the
project.

FatFs exposes two independent logical volumes. The SD card is always drive `0:`
and mounts at `/sd`. When `BANUX_INTERNAL_FLASH_FS_EN` is enabled, the 20 KB
internal Flash FAT12 disk is always drive `1:` and mounts at `/flash`. Both
mounts can exist at the same time; internal Flash is not an SD fallback switch.

The internal disk occupies `0x0807A000..0x0807EFFF`. The APP image ends at
`0x08078000` and boot configuration starts at `0x0807F000`, so firmware updates
do not erase this area. Internal Flash has limited erase endurance and is
intended for configuration and small scripts, not high-frequency logging.

STM32CubeMX normally regenerates FatFs under `app/FATFS`; after regenerating
the project, merge generated changes into this component instead of linking a
second copy.
