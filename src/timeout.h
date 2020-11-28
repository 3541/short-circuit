#pragma once

#include <a3/lpq.h>

#include "event.h"
#include "forward.h"

struct Timeout;
typedef struct Timeout Timeout;

typedef void (*TimeoutExec)(Connection*, struct io_uring*);

LPQ_IMPL_STRUCTS(Timeout);

struct Timeout {
    time_t      threshold;
    TimeoutExec time_out;
    LPQ_NODE(Timeout);
};

typedef struct TimeoutQueue {
    Event event;
    LPQ(Timeout) queue;
} TimeoutQueue;

TimeoutQueue timeout_init(void);
bool timeout_schedule(TimeoutQueue*, Timeout*, struct io_uring*);
bool timeout_handle(TimeoutQueue*, struct io_uring*, struct io_uring_cqe*);
