/*
 * SHORT CIRCUIT: EVENT HANDLE -- Batch event handler.
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

#include "event/handle.h"

#include <assert.h>
#include <liburing.h>
#include <stdint.h>

#include <a3/log.h>
#include <a3/sll.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "event/internal.h"
#include "file.h"
#include "file_handle.h"
#include "timeout.h"

#include <liburing/io_uring.h>

void event_queue_init(EventQueue* queue) {
    assert(queue);

    a3_sll_init(queue);
}

static void event_handle(Event* event, struct io_uring* uring) {
    assert(event);
    assert(uring);

    if (event->handler)
        event->handler(event->target, uring, event->handler_ctx, event->success, event->status);
    event_free(event);
}

// Handle all events pending on the queue.
static void event_queue_handle_all(EventQueue* queue, struct io_uring* uring) {
    assert(queue);
    assert(uring);

    if (io_uring_sq_space_left(uring) <= URING_SQ_LEAVE_SPACE)
        return;

    Event* event = event_from_link(a3_sll_peek(queue));
    while (event && io_uring_sq_space_left(uring) > URING_SQ_LEAVE_SPACE) {
        event = event_from_link(a3_sll_dequeue(queue));
        event_handle(event, uring);

        event = event_from_link(a3_sll_peek(queue));
    }
}

// Deliver a queue of synthetic events with a given status.
void event_synth_deliver(EventQueue* queue, struct io_uring* uring, int32_t status) {
    assert(queue);
    assert(uring);

    A3_SLL_FOR_EACH(Event, event, queue, queue_link) { event->status = status; }

    event_queue_handle_all(queue, uring);
}

// Dequeue all CQEs and handle as many as possible.
void event_handle_all(EventQueue* queue, struct io_uring* uring) {
    assert(queue);
    assert(uring);

    struct io_uring_cqe* cqe;
    for (io_uring_peek_cqe(uring, &cqe); cqe; io_uring_peek_cqe(uring, &cqe)) {
        Event* event  = io_uring_cqe_get_data(cqe);
        int    status = cqe->res;

        // Remove from the CQ.
        io_uring_cqe_seen(uring, cqe);

        if (!event) {
            if (status < 0)
                A3_ERRNO(-status, "event without target failed");
            continue;
        }

        EventTarget* target = event->target;
        // Remove from the in-flight list.
        a3_sll_remove(target, &event->queue_link);

        if (!event->target) {
            // Canceled.
            event_free(event);
            continue;
        }

        switch (event->expected_status) {
        case EXPECTED_STATUS_NONE:
            break;
        case EXPECTED_STATUS_NONNEGATIVE:
            event->success = status >= 0;
            break;
        case EXPECTED_STATUS_POSITIVE:
            event->success = status > 0;
            break;
        default:
            event->success = status == event->expected_return;
        }
        event->status = status;

        if (!event->success && event->status == -ECANCELED) {
            event_free(event);
            continue;
        }

        // Add to the to-process queue.
        a3_sll_enqueue(queue, &event->queue_link);
    }

    // Now, handle as many of the queued CQEs as possible without filling the
    // SQ.
    event_queue_handle_all(queue, uring);
}
