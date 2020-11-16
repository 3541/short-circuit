#pragma once

#include "event.h"
#include "forward.h"
#include "heap.h"


// Don't do this as a linked list. Either use a heap or just put them all on the queue.

struct Timeout;
typedef struct Timeout Timeout;

struct Timeout {
    Connection* target;
    time_t      threshold;
};

HEAP(Timeout);
HEAP_DECLARE_METHODS(Timeout);
