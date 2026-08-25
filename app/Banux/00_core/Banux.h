/**
 * @file Banux.h
 * @brief Banux boot framework single-header public API.
 *
 * Include this file from an application instead of including each Banux
 * subsystem separately.  The implementation remains in the existing
 * framework modules; this file is the stable application-facing facade.
 */
#ifndef BANUX_H
#define BANUX_H

#include "banux_config.h"
#include "debug.h"
#include "bg_shell.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*BanuxCallback_t)(void);
typedef int (*BanuxInitCallback_t)(void);

typedef struct {
    BanuxDebugWriter_t logWriter;
    const ShellIO_t *shellIo;
    BanuxCallback_t filesystemInit;
    BanuxInitCallback_t driverInit;
    BanuxCallback_t platformInit;
    BanuxCallback_t platformProcess;
} BanuxConfig_t;

/** Initialize all Banux services in their required dependency order. */
int Banux_Init(const BanuxConfig_t *config);

/** Execute one nonblocking Banux iteration, including the platform hook. */
void Banux_Process(void);

/** Legacy generic facade. Prefer Banux_Init() for board applications. */
int Banux_begin(void);

/** Execute one cooperative framework iteration and the application callback. */
void Banux_loop(void);

/** Run forever, Arduino-style. Returns only if the application callback does. */
void Banux_run(void);

/** Install application callbacks (either callback may be NULL). */
void Banux_setSetup(BanuxCallback_t callback);
void Banux_setLoop(BanuxCallback_t callback);

/** C-friendly Arduino compatibility hooks. Define these in the application. */
void setup(void);
void loop(void);
void Banux_setup(void);
void Banux_loopCallback(void);

/* Optional convenience aliases for Arduino-like sketches. */
#define BANUX_SETUP()       Banux_setup()
#define BANUX_LOOP()        Banux_loopCallback()

#ifdef __cplusplus
}
#endif

#endif /* BANUX_H */
