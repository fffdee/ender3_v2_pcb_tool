/** @file Banux.c @brief Banux application facade implementation. */
#include "Banux.h"
#include "banux_component.h"
#include "banux_io.h"
#include "command_parser.h"
#include "banux_scheduler.h"
#include "drv_init.h"
#include "bg_event.h"
#include "internal_flash_fs.h"

static BanuxCallback_t g_setup_callback;
static BanuxCallback_t g_loop_callback;
static uint8_t g_started;
static BanuxCallback_t g_platform_process;

#if defined(__GNUC__) || defined(__ARMCC_VERSION)
/* GCC 与 ARMCC V5 (AC5) 均支持 __attribute__((weak)), 应用可自行定义同名钩子 */
__attribute__((weak)) void setup(void) {}
__attribute__((weak)) void loop(void) {}
__attribute__((weak)) void Banux_setup(void) {}
__attribute__((weak)) void Banux_loopCallback(void) {}
#else
void setup(void) {}
void loop(void) {}
void Banux_setup(void) {}
void Banux_loopCallback(void) {}
#endif

int Banux_Init(const BanuxConfig_t *config)
{
    int ret;

    if (!config || !config->shellIo) return -1;
    if (g_started) return 0;

    BanuxComponent_Init();
    BanuxDebug_SetWriter(config->logWriter);
    g_platform_process = config->platformProcess;
    if (config->platformInit) config->platformInit();
    if (config->filesystemInit) config->filesystemInit();
    BG_Event_Init();
    ret = DrvFramework_Init();
    if (ret != 0) return -2;
    BanuxIo_Init();

    if (config->driverInit) {
        ret = config->driverInit();
        if (ret != 0) return -3;
    }

    (void)InternalFlashFs_Init();

    if (!Shell_Init()) return -4;
    if (!Shell_SetIO(config->shellIo)) return -5;
    Shell_RegisterAllModules();
    CommandParser_Init();
    (void)BanuxComponent_StartType(BANUX_COMPONENT_APPLICATION);

    g_started = 1u;
    Shell_Print("\r\n[APP] Banux ready: banux -i / help -a / ls / drivers / boot\r\n");
    DBG("[Banux] core initialized, Shell IO=%s\n", Shell_GetIOName());
    return 0;
}

void Banux_Process(void)
{
    if (!g_started) return;
    if (g_platform_process) g_platform_process();
    BanuxComponent_ProcessAll();
    BanuxScheduler_Process();
}

int Banux_begin(void)
{
    int ret;

    BanuxComponent_Init();
    BG_Event_Init();
    ret = DrvFramework_FullInit();
    if (ret != 0) return ret;
    BanuxIo_Init();
    CommandParser_Init();
    g_started = 1u;
    if (g_setup_callback)
        g_setup_callback();
    else {
        Banux_setup();
        setup();
    }
    return 0;
}

void Banux_loop(void)
{
    if (!g_started)
        return;

    Banux_Process();

    if (g_loop_callback)
        g_loop_callback();
    else {
        Banux_loopCallback();
        loop();
    }
}

void Banux_run(void)
{
    while (g_started)
        Banux_loop();
}

void Banux_setSetup(BanuxCallback_t callback)
{
    g_setup_callback = callback;
}

void Banux_setLoop(BanuxCallback_t callback)
{
    g_loop_callback = callback;
}
