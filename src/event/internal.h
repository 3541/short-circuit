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

#include <a3/pool.h>

#include "config.h"
#include "event.h"
#include "event/handle.h"
#include "forward.h"

// A caller can set an expected return code, either in the form of a status "class" described
// here, or a precise value.
typedef enum ExpectedStatus {
    EXPECTED_STATUS_NONE        = INT32_MIN,
    EXPECTED_STATUS_NONNEGATIVE = INT32_MIN + 1,
    EXPECTED_STATUS_POSITIVE    = INT32_MIN + 2,
} ExpectedStatus;

// An event to be submitted asynchronously.
struct Event {
    // Event queues use an inline linked list.
    A3SLink queue_link;

    bool success;
    union {
        ExpectedStatus expected_status;
        int32_t        expected_return;
        int32_t        status;
    };
    EventTarget* target;

    // On completion, the handler is called. The context variable can be anything, but will in many
    // cases be another callback to be invoked by a more general handler. See connection.c.
    EventHandler handler;
    void*        handler_ctx;
};

Event* event_from_link(A3SLink* link);
void   event_free(Event*);
