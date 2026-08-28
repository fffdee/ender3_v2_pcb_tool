/**
 * @file banux_component.h
 * @brief Static component metadata and runtime state registry.
 */
#ifndef BANUX_COMPONENT_H
#define BANUX_COMPONENT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BANUX_VERSION_STRING       "V2.0.1"
#define BANUX_COMPONENT_MAX        16u

typedef enum {
    BANUX_COMPONENT_SYSTEM = 0,
    BANUX_COMPONENT_APPLICATION
} BanuxComponentType_t;

typedef enum {
    BANUX_COMPONENT_DISABLED = 0,
    BANUX_COMPONENT_REGISTERED,
    BANUX_COMPONENT_READY,
    BANUX_COMPONENT_FAILED
} BanuxComponentState_t;

typedef int (*BanuxComponentInit_t)(void);
typedef void (*BanuxComponentProcess_t)(void);

typedef struct {
    const char *name;
    const char *version;
    const char *description;
    BanuxComponentType_t type;
    uint8_t enabled;
    BanuxComponentInit_t init;
    BanuxComponentProcess_t process;
} BanuxComponentDescriptor_t;

typedef struct {
    const BanuxComponentDescriptor_t *descriptor;
    BanuxComponentState_t state;
} BanuxComponentInfo_t;

/* Define this descriptor in the component's own directory. The central
 * catalog only references the exported symbol; component metadata remains
 * owned and versioned by the component itself. */
#define BANUX_COMPONENT_DEFINE(symbol, component_name, component_version, \
                               component_type, component_enabled, component_desc) \
    const BanuxComponentDescriptor_t symbol = {                          \
        component_name, component_version, component_desc,               \
        component_type, (uint8_t)((component_enabled) ? 1u : 0u),        \
        (BanuxComponentInit_t)0, (BanuxComponentProcess_t)0               \
    }

#define BANUX_COMPONENT_DEFINE_EX(symbol, component_name, component_version, \
                                  component_type, component_enabled,          \
                                  component_desc, init_fn, process_fn)        \
    const BanuxComponentDescriptor_t symbol = {                              \
        component_name, component_version, component_desc,                   \
        component_type, (uint8_t)((component_enabled) ? 1u : 0u),            \
        init_fn, process_fn                                                   \
    }

#define BANUX_COMPONENT_DECLARE(symbol) \
    extern const BanuxComponentDescriptor_t symbol

void BanuxComponent_Init(void);
int BanuxComponent_StartType(BanuxComponentType_t type);
void BanuxComponent_ProcessAll(void);
int BanuxComponent_SetState(const char *name, BanuxComponentState_t state);
uint8_t BanuxComponent_GetCount(void);
const BanuxComponentInfo_t *BanuxComponent_Get(uint8_t index);
const char *BanuxComponent_StateName(BanuxComponentState_t state);

#ifdef __cplusplus
}
#endif

#endif /* BANUX_COMPONENT_H */
