/*
 * SHORT CIRCUIT: TIMEOUT -- Timeouts for IO events.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <a3/ll.h>
#include <a3/util.h>

#include <sc/timeout.h>

typedef struct ScTimer {
    A3_LL(timeouts, ScTimeout) queue;
} ScTimer;

ScTimer* sc_timer_new() {
    A3_UNWRAPNI(ScTimer*, ret, calloc(1, sizeof(*ret)));
    A3_LL_INIT(&ret->queue, link);

    return ret;
}

void sc_timer_free(ScTimer* timer) {
    assert(timer);

    free(timer);
}

struct timespec const* sc_timer_next(ScTimer const* timer) {
    assert(timer);

    if (A3_LL_IS_EMPTY(&timer->queue))
        return NULL;
    return &A3_LL_HEAD(&timer->queue)->deadline;
}

static ssize_t sc_timespec_compare(struct timespec lhs, struct timespec rhs) {
    return (lhs.tv_sec != rhs.tv_sec) ? lhs.tv_sec - rhs.tv_sec : lhs.tv_nsec - rhs.tv_nsec;
}

void sc_timer_tick(ScTimer* timer) {
    assert(timer);

    struct timespec now;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &now));

    A3_LL_FOR_EACH(ScTimeout, timeout, &timer->queue, link) {
        if (sc_timespec_compare(now, timeout->deadline) < 0)
            break;

        A3_LL_REMOVE(timeout, link);
        timeout->done(timeout);
    }
}

static struct timespec sc_time_delay_to_timespec_deadline(time_t time_s) {
    struct timespec ret;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &ret));
    ret.tv_sec += time_s;

    return ret;
}

void sc_timeout_init(ScTimeout* timeout, ScTimeoutCb done_cb, time_t delay_s) {
    *timeout = (ScTimeout) { .deadline = sc_time_delay_to_timespec_deadline(delay_s),
                             .delay_s  = delay_s,
                             .done     = done_cb };
}

void sc_timeout_add(ScTimer* timer, ScTimeout* timeout) {
    assert(timer);
    assert(timeout);

    if (A3_LL_IS_EMPTY(&timer->queue)) {
        A3_LL_ENQUEUE(&timer->queue, timeout, link);
        return;
    }

    if (sc_timespec_compare(timeout->deadline, A3_LL_HEAD(&timer->queue)->deadline) <= 0) {
        A3_LL_PUSH(&timer->queue, timeout, link);
        return;
    }

    ScTimeout* before = NULL;
    A3_LL_FOR_EACH_REV(ScTimeout, prev, &timer->queue, link) {
        if (sc_timespec_compare(timeout->deadline, prev->deadline) >= 0) {
            before = prev;
            break;
        }
    }
    A3_LL_INSERT_AFTER(before, timeout, link);
}

void sc_timeout_reset(ScTimeout* timeout) {
    assert(timeout);

    struct timespec new_deadline = sc_time_delay_to_timespec_deadline(timeout->delay_s);
    assert(sc_timespec_compare(timeout->deadline, new_deadline) <= 0);
    timeout->deadline = new_deadline;

    ScTimeout* next = A3_LL_NEXT(timeout, link);
    if (!next)
        return;

    A3_LL_REMOVE(timeout, link);
    ScTimeout* current = next;
    next               = A3_LL_NEXT(next, link);
    while (next && sc_timespec_compare(timeout->deadline, next->deadline) > 0) {
        next    = A3_LL_NEXT(next, link);
        current = next;
    }

    A3_LL_INSERT_AFTER(current, timeout, link);
}

void sc_timeout_cancel(ScTimeout* timeout) {
    assert(timeout);

    A3_LL_REMOVE(timeout, link);
}
