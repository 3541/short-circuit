#include "timeout.h"

LPQ_DECLARE_METHODS(Timeout);

INLINE ssize_t timeout_compare(Timeout* lhs, Timeout* rhs) {
    assert(lhs);
    assert(rhs);
    return lhs->threshold - rhs->threshold;
}

LPQ_IMPL_METHODS(Timeout, timeout_compare);

TimeoutQueue timeout_init() {
    TimeoutQueue ret;
    LPQ_INIT(Timeout)(&ret.queue);
    return ret;
}

bool timeout_schedule(TimeoutQueue* this, Timeout* timeout,
                      struct io_uring* uring) {
    assert(this);
    assert(timeout);
    assert(!timeout->_lpq_ptr.next && !timeout->_lpq_ptr.prev);

    LPQ_ENQUEUE(Timeout)(&this->queue, timeout);
    if (LPQ_PEEK(Timeout)(&this->queue) == timeout)
        return event_timeout_submit(&this->event, uring, timeout->threshold, 0,
                                    IORING_TIMEOUT_ABS);
    return true;
}
