#pragma once

#include <a3/lpq.h>

#include "event.h"
#include "forward.h"

typedef struct __kernel_timespec Timespec;

struct Timeout;
typedef struct Timeout Timeout;

typedef bool (*TimeoutExec)(Timeout*, struct io_uring*);

LPQ_IMPL_STRUCTS(Timeout);

struct Timeout {
    Timespec    threshold;
    TimeoutExec fire;
    LPQ_NODE(Timeout);
};

typedef struct TimeoutQueue {
    Event event;
    LPQ(Timeout) queue;
} TimeoutQueue;

void timeout_queue_init(TimeoutQueue*);
bool timeout_schedule(TimeoutQueue*, Timeout*, struct io_uring*);
bool timeout_handle(TimeoutQueue*, struct io_uring*, struct io_uring_cqe*);
