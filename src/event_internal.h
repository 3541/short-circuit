#pragma once

#include <stdint.h>

#include "event.h"

typedef struct Event {
    EventType    type;
    int32_t      status;
    EventTarget* target;
    SLL_NODE(Event);
} Event;

SLL_DECLARE_METHODS(Event);

void event_free(Event*);
