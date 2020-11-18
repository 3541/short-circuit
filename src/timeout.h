#pragma once

#include "event.h"
#include "forward.h"
#include "pq.h"

struct Timeout;
typedef struct Timeout Timeout;

struct Timeout {
    Connection* target;
    time_t      threshold;
};

PQ(Timeout);
