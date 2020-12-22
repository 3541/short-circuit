#pragma once

typedef struct Event {
    EventType    type;
    int32_t      status;
    EventTarget* target;
    SLL_NODE(Event);
} Event;
