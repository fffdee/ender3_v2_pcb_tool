# Banux core

`00_core` owns the framework kernel rather than product or hardware features.

- `Banux.c/.h`: framework facade and cooperative application lifecycle.
- `banux_scheduler.c/.h`: nonblocking system-service scheduling.
- `banux_debug.c`, `debug.h`: platform-independent injectable logging.
- `banux_component.c/.h`: static system/application component registry and state.
- `banux_config.h`: common feature switches and framework limits.

Concrete hardware drivers belong in `01_driver`; system components belong in
`02_system_components`; reusable application components belong in
`03_application_components`; product orchestration belongs in `04_application`.
