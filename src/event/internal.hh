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

struct Event {
    A3_POOL_ALLOCATED_PRIV_NEW(Event)
public:
    A3_SLL_NODE(Event);

    static constexpr int32_t EXPECT_NONE        = -255;
    static constexpr int32_t EXPECT_NONNEGATIVE = -254;
    static constexpr int32_t EXPECT_POSITIVE    = -253;

private:
    EventType    type { EVENT_INVALID };
    int32_t      status { 0 };
    uint32_t     flags { 0 };
    EventTarget* target { nullptr };

    // For access to the status.
    friend void event_synth_deliver(EventQueue*, struct io_uring*, int32_t status);
    // For access to the target and status.
    friend void event_handle_all(EventQueue*, struct io_uring*);
    // For initialization.
    friend Event* event_create(EventTarget*, EventType);

    Event(EventTarget*, EventType, int32_t expected_return, bool chain, uint32_t flags,
          bool force_handle, bool queue);
    Event(const Event&) = delete;
    Event(Event&&)      = delete;

    void set_failed(bool failed) {
        if (failed)
            flags |= EVENT_FLAG_FAIL;
    }
    bool canceled() const { return !target; }

public:
    static std::unique_ptr<Event> create(EventTarget* target, EventType ty, int32_t expected_return,
                                         bool chain = false, uint32_t flags = 0,
                                         bool force_handle = false, bool queue = true) {
        // The Event must be explicitly constructed because `make_unique` cannot
        // see the constructor.
        return std::unique_ptr<Event> { new Event { target, ty, expected_return, chain, flags,
                                                    force_handle, queue } };
    }

    void handle(struct io_uring&);
    void cancel() { target = nullptr; }
};
