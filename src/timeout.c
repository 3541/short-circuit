#include "timeout.h"

LPQ_DECLARE_METHODS(Timeout);

INLINE ssize_t timeout_compare(Timeout* lhs, Timeout* rhs) {
    assert(lhs);
    assert(rhs);
    return (lhs->threshold.tv_sec != rhs->threshold.tv_sec)
               ? lhs->threshold.tv_sec - rhs->threshold.tv_sec
               : lhs->threshold.tv_nsec - rhs->threshold.tv_nsec;
}

// Compare a kernel timespec and libc timespec.
INLINE ssize_t timespec_compare(Timespec lhs, struct timespec rhs) {
    return (lhs.tv_sec != rhs.tv_sec) ? lhs.tv_sec - rhs.tv_sec
                                      : lhs.tv_nsec - rhs.tv_nsec;
}

LPQ_IMPL_METHODS(Timeout, timeout_compare);

void timeout_queue_init(TimeoutQueue* this) {
    assert(this);
    LPQ_INIT(Timeout)(&this->queue);
}

static bool timeout_schedule_next(TimeoutQueue* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    Timeout* next = LPQ_PEEK(Timeout)(&this->queue);
    if (!next)
        return true;

    return event_timeout_submit(&this->event, uring, &next->threshold,
                                IORING_TIMEOUT_ABS);
}

bool timeout_schedule(TimeoutQueue* this, Timeout* timeout,
                      struct io_uring* uring) {
    assert(this);
    assert(timeout);
    assert(uring);
    assert(!timeout->_lpq_ptr.next && !timeout->_lpq_ptr.prev);

    LPQ_ENQUEUE(Timeout)(&this->queue, timeout);
    if (LPQ_PEEK(Timeout)(&this->queue) == timeout)
        return timeout_schedule_next(this, uring);

    return true;
}

bool timeout_is_scheduled(Timeout* this) {
    assert(this);

    return LPQ_IS_INSERTED(Timeout)(this);
}

bool timeout_cancel(Timeout* this) {
    assert(this);
    assert(this->_lpq_ptr.next && this->_lpq_ptr.prev);

    // There is no need to actually fiddle with events here.
    LPQ_REMOVE(Timeout)(this);

    return true;
}

bool timeout_handle(TimeoutQueue* this, struct io_uring* uring,
                    struct io_uring_cqe* cqe) {
    assert(this);
    assert(uring);
    assert(cqe);
    assert(cqe->res > 0 || cqe->res == -ETIME);
    (void) cqe;

    log_msg(TRACE, "Timeout firing.");

    struct timespec current;
    UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &current));

    Timeout* peek;
    while ((peek = LPQ_PEEK(Timeout)(&this->queue)) &&
           timespec_compare(peek->threshold, current) <= 0) {
        Timeout* timeout = LPQ_DEQUEUE(Timeout)(&this->queue);
        TRYB(timeout->fire(timeout, uring));
    }

    return timeout_schedule_next(this, uring);
}
