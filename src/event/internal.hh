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

#include "event.h"
#include "event/handle.h"
#include "forward.h"

struct Event {
    EventType type { EVENT_INVALID };
    int32_t   status { 0 };
    uintptr_t target_ptr { 0 };
    SLL_NODE(Event);

    // For access to the target.
    friend void event_handle_all(EventQueue* queue, struct io_uring* uring);

private:
    Event(EventTarget*, EventType, bool chain, bool ignore);

    static void* operator new(size_t size) noexcept;

    EventTarget* target() {
        return reinterpret_cast<EventTarget*>(target_ptr &
                                              ~(EVENT_CHAIN | EVENT_IGNORE));
    }

    bool chain() { return target_ptr & EVENT_CHAIN; }
    bool ignore() { return target_ptr & EVENT_IGNORE; }

public:
    static void operator delete(void*);

    static std::unique_ptr<Event> create(EventTarget* target, EventType ty,
                                         bool chain  = false,
                                         bool ignore = false) {
        // The Event must be explicitly constructed because `make_unique` cannot
        // see the constructor.
        return std::unique_ptr<Event> { new Event { target, ty, chain,
                                                    ignore } };
    }

    void handle(struct io_uring&);
    void cancel() { target_ptr = 0; }
    bool canceled() { return !target_ptr; }
};

SLL_DECLARE_METHODS(Event);