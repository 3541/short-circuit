/*
 * SHORT CIRCUIT: LISTEN -- Socket listener. Keeps an accept event queued on a
 * given socket.
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

#include <liburing.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>

#include "connection.h"
#include "forward.h"

typedef struct Listener {
    fd                  socket;
    ConnectionTransport transport;
    bool                accept_queued;
} Listener;

void        listener_init(Listener*, in_port_t, ConnectionTransport);
Connection* listener_accept_submit(Listener*, struct io_uring*);
void listener_accept_all(Listener*, size_t n_listeners, struct io_uring*);
