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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <a3/cpp.h>
#include <a3/ll.h>

#include "event.h"
#include "forward.h"

H_BEGIN

struct Timeout;
typedef struct Timeout Timeout;

typedef bool (*TimeoutExec)(Timeout*, struct io_uring*);

LL_DEFINE_STRUCTS(Timeout)

struct Timeout {
    Timespec    threshold;
    TimeoutExec fire;
    LL_NODE(Timeout);
};

typedef struct TimeoutQueue {
    EVENT_TARGET;
    LL(Timeout) queue;
} TimeoutQueue;

void timeout_queue_init(TimeoutQueue*);
bool timeout_schedule(TimeoutQueue*, Timeout*, struct io_uring*);
bool timeout_is_scheduled(Timeout*);
bool timeout_cancel(Timeout*);
bool timeout_event_handle(TimeoutQueue*, struct io_uring*, int32_t status);

H_END
