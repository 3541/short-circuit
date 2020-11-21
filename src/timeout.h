#pragma once

#include <a3/pq.h>

#include "event.h"
#include "forward.h"

struct Timeout;
typedef struct Timeout Timeout;

struct Timeout {
    Connection* target;
    time_t      threshold;
};

PQ(Timeout);
