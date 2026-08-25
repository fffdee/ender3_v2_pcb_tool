#include "banux_component.h"

/* Event sources are present in the repository but not linked by this APP. */
BANUX_COMPONENT_DEFINE(g_banux_component_event_bus,
                       "event_bus", "1.0.0", BANUX_COMPONENT_SYSTEM, 0,
                       "publish/subscribe message system");
