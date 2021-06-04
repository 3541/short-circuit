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

static bool timeout_schedule_next(TimeoutQueue*, struct io_uring*);

void timeout_queue_init(TimeoutQueue* timeouts) {
    assert(timeouts);
    A3_LL_INIT(Timeout)(&timeouts->queue);
}

static void timeout_handle(EventTarget* target, struct io_uring* uring, void* ctx, bool success,
                           int32_t status) {
    assert(target);
    assert(uring);
    assert(status > 0 || status == -ETIME);
    (void)ctx;
    (void)success;
    (void)status;

    TimeoutQueue* timeouts = EVT_PTR(target, TimeoutQueue);

    a3_log_msg(LOG_TRACE, "Timeout firing.");

    struct timespec current;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &current));

    Timeout* peek;
    while ((peek = A3_LL_PEEK(Timeout)(&timeouts->queue)) &&
           timespec_compare(peek->threshold, current) <= 0) {
        Timeout* timeout = A3_LL_DEQUEUE(Timeout)(&timeouts->queue);
        timeout->fire(timeout, uring);
    }

    if (!timeout_schedule_next(timeouts, uring))
        a3_log_msg(LOG_ERROR, "Unable to schedule timeout.");
}

static bool timeout_schedule_next(TimeoutQueue* timeouts, struct io_uring* uring) {
    assert(timeouts);
    assert(uring);

    Timeout* next = A3_LL_PEEK(Timeout)(&timeouts->queue);
    if (!next)
        return true;

    return event_timeout_submit(EVT(timeouts), uring, timeout_handle, NULL, &next->threshold,
                                IORING_TIMEOUT_ABS);
}

bool timeout_schedule(TimeoutQueue* timeouts, Timeout* timeout, struct io_uring* uring) {
    assert(timeouts);
    assert(timeout);
    assert(uring);
    assert(!timeout->_a3_ll_ptr.next && !timeout->_a3_ll_ptr.prev);

    A3_LL_ENQUEUE(Timeout)(&timeouts->queue, timeout);
    if (A3_LL_PEEK(Timeout)(&timeouts->queue) == timeout)
        return timeout_schedule_next(timeouts, uring);

    return true;
}

bool timeout_is_scheduled(Timeout* timeouts) {
    assert(timeouts);

    return A3_LL_IS_INSERTED(Timeout)(timeouts);
}

bool timeout_cancel(Timeout* timeouts) {
    assert(timeouts);
    assert(timeouts->_a3_ll_ptr.next && timeouts->_a3_ll_ptr.prev);

    // There is no need to actually fiddle with events here.
    A3_LL_REMOVE(Timeout)(timeouts);

    return true;
}
