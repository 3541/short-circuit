/*
 * SHORT CIRCUIT: TIMEOUT -- Timeout queue using the uring.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "timeout.h"

#include <assert.h>
#include <errno.h>
#include <liburing/io_uring.h>
#include <sys/types.h>
#include <time.h>

#include <a3/ll.h>
#include <a3/log.h>
#include <a3/util.h>

#include "event.h"
#include "forward.h"

A3_LL_DECLARE_METHODS(Timeout)

// Compare a kernel timespec and libc timespec.
static ssize_t timespec_compare(Timespec lhs, struct timespec rhs) {
    return (lhs.tv_sec != rhs.tv_sec) ? lhs.tv_sec - rhs.tv_sec : lhs.tv_nsec - rhs.tv_nsec;
}

A3_LL_DEFINE_METHODS(Timeout)

void timeout_queue_init(TimeoutQueue* this) {
    assert(this);
    A3_LL_INIT(Timeout)(&this->queue);
}

static bool timeout_schedule_next(TimeoutQueue* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    Timeout* next = A3_LL_PEEK(Timeout)(&this->queue);
    if (!next)
        return true;

    return event_timeout_submit(EVT(this), uring, &next->threshold, IORING_TIMEOUT_ABS);
}

bool timeout_schedule(TimeoutQueue* this, Timeout* timeout, struct io_uring* uring) {
    assert(this);
    assert(timeout);
    assert(uring);
    assert(!timeout->_a3_ll_ptr.next && !timeout->_a3_ll_ptr.prev);

    A3_LL_ENQUEUE(Timeout)(&this->queue, timeout);
    if (A3_LL_PEEK(Timeout)(&this->queue) == timeout)
        return timeout_schedule_next(this, uring);

    return true;
}

bool timeout_is_scheduled(Timeout* this) {
    assert(this);

    return A3_LL_IS_INSERTED(Timeout)(this);
}

bool timeout_cancel(Timeout* this) {
    assert(this);
    assert(this->_a3_ll_ptr.next && this->_a3_ll_ptr.prev);

    // There is no need to actually fiddle with events here.
    A3_LL_REMOVE(Timeout)(this);

    return true;
}

bool timeout_event_handle(TimeoutQueue* this, struct io_uring* uring, int32_t status) {
    assert(this);
    assert(uring);
    assert(status > 0 || status == -ETIME);
    (void)status;

    a3_log_msg(TRACE, "Timeout firing.");

    struct timespec current;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &current));

    Timeout* peek;
    while ((peek = A3_LL_PEEK(Timeout)(&this->queue)) &&
           timespec_compare(peek->threshold, current) <= 0) {
        Timeout* timeout = A3_LL_DEQUEUE(Timeout)(&this->queue);
        A3_TRYB(timeout->fire(timeout, uring));
    }

    return timeout_schedule_next(this, uring);
}
