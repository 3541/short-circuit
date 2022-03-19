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
#include <sys/types.h>
#include <time.h>

#include <a3/ll.h>
#include <a3/log.h>
#include <a3/util.h>

#include "event.h"
#include "forward.h"

#include <liburing/io_uring.h>

// Compare a kernel timespec and libc timespec.
static ssize_t timespec_compare(Timespec lhs, struct timespec rhs) {
    return (lhs.tv_sec != rhs.tv_sec) ? lhs.tv_sec - rhs.tv_sec : lhs.tv_nsec - rhs.tv_nsec;
}

static bool timeout_schedule_next(TimeoutQueue*, struct io_uring*);

void timeout_queue_init(TimeoutQueue* timeouts) {
    assert(timeouts);
    a3_ll_init(&timeouts->queue);
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

    A3_TRACE("Timeout firing.");

    struct timespec current;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &current));

    A3LL* peek;
    while ((peek = a3_ll_peek(&timeouts->queue)) &&
           timespec_compare(A3_CONTAINER_OF(peek, Timeout, queue_link)->threshold, current) <= 0) {
        Timeout* timeout = A3_CONTAINER_OF(a3_ll_dequeue(&timeouts->queue), Timeout, queue_link);
        timeout->fire(timeout, uring);
    }

    if (!timeout_schedule_next(timeouts, uring))
        A3_ERROR("Unable to schedule timeout.");
}

static bool timeout_schedule_next(TimeoutQueue* timeouts, struct io_uring* uring) {
    assert(timeouts);
    assert(uring);

    A3LL* next = a3_ll_peek(&timeouts->queue);
    if (!next)
        return true;

    return event_timeout_submit(EVT(timeouts), uring, timeout_handle, NULL,
                                &A3_CONTAINER_OF(next, Timeout, queue_link)->threshold,
                                IORING_TIMEOUT_ABS);
}

bool timeout_schedule(TimeoutQueue* timeouts, Timeout* timeout, struct io_uring* uring) {
    assert(timeouts);
    assert(timeout);
    assert(uring);
    assert(!timeout_is_scheduled(timeout));

    a3_ll_enqueue(&timeouts->queue, &timeout->queue_link);
    if (a3_ll_peek(&timeouts->queue) == &timeout->queue_link)
        return timeout_schedule_next(timeouts, uring);

    return true;
}

bool timeout_is_scheduled(Timeout* timeout) {
    assert(timeout);

    return timeout->queue_link.next && timeout->queue_link.prev;
}

bool timeout_cancel(Timeout* timeout) {
    assert(timeout);
    assert(timeout_is_scheduled(timeout));

    // There is no need to actually fiddle with events here.
    a3_ll_remove(&timeout->queue_link);

    return true;
}
