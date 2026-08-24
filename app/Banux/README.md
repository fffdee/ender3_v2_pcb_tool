# Banux library facade

Applications can use the boot framework like an Arduino library:

```c
#include "Banux.h"

void setup(void) {
    /* application setup */
}

void loop(void) {
    /* application work */
}

int main(void) {
    if (Banux_begin() != 0)
        return -1;
    Banux_run();
    return 0;
}
```

For projects that prefer weak hooks, implement `Banux_setup()` and
`Banux_loopCallback()` and call `Banux_begin()`/`Banux_run()` directly.

`Banux.h` also re-exports the HAL, driver framework, event and system-state
headers, so existing APIs remain available from one include.
