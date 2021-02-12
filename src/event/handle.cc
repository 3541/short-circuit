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

#include <cassert>
#include <cstdint>
#include <liburing.h>
#include <liburing/io_uring.h>

#include <a3/log.h>
#include <a3/sll.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "event/internal.hh"
#include "file.h"
#include "file_handle.h"
#include "timeout.h"

void event_queue_init(EventQueue* queue) {
    assert(queue);

    A3_SLL_INIT(Event)(queue);
}

void Event::handle(struct io_uring& uring) {
    auto*     target      = this->target();
    bool      chain       = this->chain();
    bool      success     = !failed();
    int32_t   status_code = status;
    EventType ty          = type;

    delete this;

    switch (ty) {
    case EVENT_ACCEPT:
    case EVENT_CLOSE:
    case EVENT_OPENAT_SYNTH:
    case EVENT_READ:
    case EVENT_RECV:
    case EVENT_SEND:
    case EVENT_SPLICE:
        connection_event_handle(EVT_PTR(target, Connection), &uring, ty, success, status_code,
                                chain);
        return;
    case EVENT_OPENAT:
        file_handle_event_handle(EVT_PTR(target, FileHandle), &uring, status_code);
        return;
    case EVENT_TIMEOUT:
        timeout_event_handle(EVT_PTR(target, TimeoutQueue), &uring, status_code);
        return;
    case EVENT_INVALID:
        return;
    }

    A3_UNREACHABLE();
}

// Handle all events pending on the queue.
void event_queue_handle_all(EventQueue* queue, struct io_uring* uring) {
    assert(queue);
    assert(uring);

    if (io_uring_sq_space_left(uring) <= URING_SQ_LEAVE_SPACE)
        return;

    auto* event = A3_SLL_PEEK(Event)(queue);
    while (event && io_uring_sq_space_left(uring) > URING_SQ_LEAVE_SPACE) {
        event = A3_SLL_DEQUEUE(Event)(queue);
        event->handle(*uring);

        event = A3_SLL_PEEK(Event)(queue);
    }
}

// Deliver a queue of synthetic events with a given status.
void event_synth_deliver(EventQueue* queue, struct io_uring* uring, int32_t status) {
    assert(queue);
    assert(uring);

    for (auto* event = A3_SLL_PEEK(Event)(queue); event; event = A3_SLL_NEXT(Event)(event))
        event->status = status;

    event_queue_handle_all(queue, uring);
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

        if (!event) {
            if (status < 0)
                a3_log_error(-status, "event without target failed");
            continue;
        }

        if (event->canceled()) {
            delete event;
            continue;
        }

        switch (event->status) {
        case Event::EXPECT_NONE:
            break;
        case Event::EXPECT_NONNEGATIVE:
            event->set_failed(status < 0);
            break;
        case Event::EXPECT_POSITIVE:
            event->set_failed(status <= 0);
            break;
        default:
            event->set_failed(status != event->status);
            break;
        }
        event->status = status;

        auto* target = event->target();

        // Remove from the in-flight list.
        A3_SLL_REMOVE(Event)(target, event);

        // Add to the to-process queue.
        A3_SLL_ENQUEUE(Event)(queue, event);
    }

    // Now, handle as many of the queued CQEs as possible without filling the
    // SQ.
    event_queue_handle_all(queue, uring);
}
