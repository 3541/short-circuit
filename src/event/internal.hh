/*
 * SHORT CIRCUIT: EVENT INTERNAL -- Private event handling types and utilities.
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

#pragma once

#include <cstdint>
#include <memory>

#include <a3/pool.h>

#include "config.h"
#include "event.h"
#include "event/handle.h"
#include "forward.h"

// An event to be submitted asynchronously.
struct Event {
    A3_POOL_ALLOCATED_PRIV_NEW(Event)
public:
    // Event queues use an inline linked list.
    A3SLink queue_link { nullptr };

    // A caller can set an expected return code, either in the form of a status "class" described
    // here, or a precise value.
    enum class ExpectedStatus : int32_t {
        None        = INT32_MIN,
        Nonnegative = INT32_MIN + 1,
        Positive    = INT32_MIN + 2,
    };

private:
    bool success { true };
    union {
        ExpectedStatus expected_status;
        int32_t        expected_return;
        int32_t        status;
    };
    EventTarget* target { nullptr };

    // On completion, the handler is called. The context variable can be anything, but will in many
    // cases be another callback to be invoked by a more general handler. See connection.c.
    EventHandler handler { nullptr };
    void*        handler_ctx { nullptr };

    // For access to the status.
    friend void event_synth_deliver(EventQueue*, struct io_uring*, int32_t status);
    // For access to the target and status.
    friend void event_handle_all(EventQueue*, struct io_uring*);
    // For initialization.
    friend Event* event_create(EventTarget*, EventHandler, void*);

    Event(EventTarget*, EventHandler, void* cb_data, int32_t expected_return, bool queue);
    Event(Event const&) = delete;
    Event(Event&&)      = delete;

    Event& operator=(Event const&) = delete;
    Event& operator=(Event&&) = delete;

    void set_failed(bool failed) { success = !failed; }

    void set_status(int32_t new_status) {
        switch (expected_status) {
        case ExpectedStatus::None:
            break;
        case ExpectedStatus::Nonnegative:
            set_failed(new_status < 0);
            break;
        case ExpectedStatus::Positive:
            set_failed(new_status <= 0);
            break;
        default:
            set_failed(new_status != expected_return);
        }

        status = new_status;
    }

    bool canceled() const { return !target; }

public:
    static std::unique_ptr<Event> create(EventTarget* target, EventHandler handler,
                                         void* handler_ctx, int32_t expected_return,
                                         bool queue = true) {
        // The Event must be explicitly constructed because `make_unique` cannot
        // see the constructor.
        return std::unique_ptr<Event> { new Event { target, handler, handler_ctx, expected_return,
                                                    queue } };
    }

    static std::unique_ptr<Event> create(EventTarget* target, EventHandler handler,
                                         void* handler_ctx, ExpectedStatus expected_status,
                                         bool queue = true) {
        return create(target, handler, handler_ctx, static_cast<int32_t>(expected_status), queue);
    }

    void cancel() { target = nullptr; }

    void handle(struct io_uring&);

    static Event* from_link(A3SLink* link) { return A3_CONTAINER_OF(link, Event, queue_link); }
};
