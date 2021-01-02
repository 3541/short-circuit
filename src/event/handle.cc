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
#include <liburing/io_uring.h>

#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event/internal.hh"
#include "timeout.h"

void event_queue_init(EventQueue* queue) {
    assert(queue);

    SLL_INIT(Event)(queue);
}

void Event::handle(struct io_uring& uring) {
    auto*     target      = this->target();
    bool      chain       = this->chain();
    bool      ignore      = this->ignore();
    int32_t   status_code = status;
    EventType ty          = type;

    delete this;

    if (ignore) {
        if (status_code < 0)
            log_error(-status_code, "ignored event failed");
        return;
    }

    switch (ty) {
    case EVENT_ACCEPT:
    case EVENT_CLOSE:
    case EVENT_OPENAT:
    case EVENT_READ:
    case EVENT_RECV:
    case EVENT_SEND:
    case EVENT_SPLICE:
        connection_event_handle(reinterpret_cast<Connection*>(target), &uring,
                                ty, status_code, chain);
        return;
    case EVENT_TIMEOUT:
        timeout_handle(reinterpret_cast<TimeoutQueue*>(target), &uring, status);
        return;
    case EVENT_CANCEL:
    case EVENT_INVALID:
        return;
    }

    UNREACHABLE();
}

// Dequeue all CQEs and handle as many as possible.
void event_handle_all(EventQueue* queue, struct io_uring* uring) {
    assert(queue);
    assert(uring);

    struct io_uring_cqe* cqe;
    for (io_uring_peek_cqe(uring, &cqe); cqe; io_uring_peek_cqe(uring, &cqe)) {
        auto* event  = static_cast<Event*>(io_uring_cqe_get_data(cqe));
        int   status = cqe->res;

        // Remove from the CQ.
        io_uring_cqe_seen(uring, cqe);

        if (!event)
            continue;
        event->status = status;

        auto* target = event->target();

        // Remove from the in-flight list.
        SLL_REMOVE(Event)(target, event);

        // Add to the to-process queue.
        SLL_ENQUEUE(Event)(queue, event);
    }

    // Now, handle as many of the queued CQEs as possible without filling the
    // SQ.
    if (io_uring_sq_space_left(uring) <= URING_SQ_LEAVE_SPACE)
        return;
    for (auto* event = SLL_DEQUEUE(Event)(queue);
         event && io_uring_sq_space_left(uring) > URING_SQ_LEAVE_SPACE;
         event = SLL_POP(Event)(queue))
        event->handle(*uring);

    return;
}
