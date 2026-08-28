#include "banux_component.h"
#include "banux_config.h"

/* Event sources are present in the repository but not linked by this APP. */
BANUX_COMPONENT_DEFINE(g_banux_component_event_bus,
                       "event_bus", "1.1.0", BANUX_COMPONENT_SYSTEM,
                       BG_EVENT_EN,
                       "publish/subscribe message system");
