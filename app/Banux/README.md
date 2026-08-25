# Banux library facade

Board applications initialize Banux through the core facade. Hardware and
middleware remain outside core and are supplied as callbacks:

```c
#include "Banux.h"

int main(void) {
    const BanuxConfig_t config = {
        app_log, ShellIO_UartAll_Get(), MX_FATFS_Init,
        BanuxDriver_RegisterAll, app_bl_init, app_bl_poll
    };

    if (Banux_Init(&config) != 0)
        return -1;
    for (;;) Banux_Process();
    return 0;
}
```

`Banux_Init()` owns subsystem ordering. The STM32Cube/Elm-Chan FatFs component
lives under `02_system_components/fatfs` and is initialized through
`MX_FATFS_Init()`; no second FAT32 implementation is linked.
