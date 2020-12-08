#pragma once

#include <a3/ll.h>

#include "event.h"
#include "forward.h"

typedef struct __kernel_timespec Timespec;

struct Timeout;
typedef struct Timeout Timeout;

typedef bool (*TimeoutExec)(Timeout*, struct io_uring*);

LL_IMPL_STRUCTS(Timeout);

struct Timeout {
    Timespec    threshold;
    TimeoutExec fire;
    LL_NODE(Timeout);
};

typedef struct TimeoutQueue {
    Event event;
    LL(Timeout) queue;
} TimeoutQueue;

void timeout_queue_init(TimeoutQueue*);
bool timeout_schedule(TimeoutQueue*, Timeout*, struct io_uring*);
bool timeout_is_scheduled(Timeout*);
bool timeout_cancel(Timeout*);
bool timeout_handle(TimeoutQueue*, struct io_uring*, struct io_uring_cqe*);
