#pragma once

#include <a3/lpq.h>

#include "event.h"
#include "forward.h"

struct Timeout;
typedef struct Timeout Timeout;

LPQ_IMPL_STRUCTS(Timeout);

struct Timeout {
    time_t      threshold;
    LPQ_NODE(Timeout);
};
