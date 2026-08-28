/** @file banux_component.c @brief Banux static component registry. */
#include <string.h>
#include "banux_component.h"

BANUX_COMPONENT_DECLARE(g_banux_component_vfs);
BANUX_COMPONENT_DECLARE(g_banux_component_driver_framework);
BANUX_COMPONENT_DECLARE(g_banux_component_file_io);
BANUX_COMPONENT_DECLARE(g_banux_component_shell);
BANUX_COMPONENT_DECLARE(g_banux_component_command_parser);
BANUX_COMPONENT_DECLARE(g_banux_component_fatfs);
BANUX_COMPONENT_DECLARE(g_banux_component_internal_flash_fs);
BANUX_COMPONENT_DECLARE(g_banux_component_event_bus);
BANUX_COMPONENT_DECLARE(g_banux_component_firmware_upgrade);
BANUX_COMPONENT_DECLARE(g_banux_component_gcode);
BANUX_COMPONENT_DECLARE(g_banux_component_wireless_control);

static const BanuxComponentDescriptor_t *const g_static_components[] = {
    &g_banux_component_vfs,
    &g_banux_component_driver_framework,
    &g_banux_component_file_io,
    &g_banux_component_shell,
    &g_banux_component_command_parser,
    &g_banux_component_fatfs,
    &g_banux_component_internal_flash_fs,
    &g_banux_component_event_bus,
    &g_banux_component_firmware_upgrade,
    &g_banux_component_gcode,
    &g_banux_component_wireless_control
};

static BanuxComponentInfo_t g_components[BANUX_COMPONENT_MAX];
static uint8_t g_component_count;

void BanuxComponent_Init(void)
{
    uint8_t i;
    uint8_t count = (uint8_t)(sizeof(g_static_components) /
                              sizeof(g_static_components[0]));

    memset(g_components, 0, sizeof(g_components));
    g_component_count = count > BANUX_COMPONENT_MAX ? BANUX_COMPONENT_MAX : count;

    for (i = 0; i < g_component_count; i++) {
        g_components[i].descriptor = g_static_components[i];
        g_components[i].state = g_static_components[i]->enabled
                              ? BANUX_COMPONENT_REGISTERED
                              : BANUX_COMPONENT_DISABLED;
    }
}

int BanuxComponent_StartType(BanuxComponentType_t type)
{
    uint8_t i;
    int failures = 0;

    for (i = 0u; i < g_component_count; i++) {
        const BanuxComponentDescriptor_t *descriptor = g_components[i].descriptor;
        int result;

        if (descriptor->type != type || !descriptor->enabled || !descriptor->init) {
            continue;
        }
        result = descriptor->init();
        g_components[i].state = result == 0
                              ? BANUX_COMPONENT_READY
                              : BANUX_COMPONENT_FAILED;
        if (result != 0) failures++;
    }
    return failures ? -failures : 0;
}

void BanuxComponent_ProcessAll(void)
{
    uint8_t i;

    for (i = 0u; i < g_component_count; i++) {
        const BanuxComponentDescriptor_t *descriptor = g_components[i].descriptor;
        if (g_components[i].state == BANUX_COMPONENT_READY && descriptor->process) {
            descriptor->process();
        }
    }
}

int BanuxComponent_SetState(const char *name, BanuxComponentState_t state)
{
    uint8_t i;

    if (!name) return -1;
    for (i = 0; i < g_component_count; i++) {
        if (strcmp(g_components[i].descriptor->name, name) == 0) {
            if (!g_components[i].descriptor->enabled &&
                state != BANUX_COMPONENT_DISABLED) {
                return -2;
            }
            g_components[i].state = state;
            return 0;
        }
    }
    return -3;
}

uint8_t BanuxComponent_GetCount(void)
{
    return g_component_count;
}

const BanuxComponentInfo_t *BanuxComponent_Get(uint8_t index)
{
    return index < g_component_count ? &g_components[index] : 0;
}

const char *BanuxComponent_StateName(BanuxComponentState_t state)
{
    switch (state) {
        case BANUX_COMPONENT_DISABLED:   return "disabled";
        case BANUX_COMPONENT_REGISTERED: return "registered";
        case BANUX_COMPONENT_READY:      return "ready";
        case BANUX_COMPONENT_FAILED:     return "failed";
        default:                         return "unknown";
    }
}
