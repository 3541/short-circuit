/*
 * SHORT CIRCUIT: LISTEN -- Socket listener. Keeps an accept event queued on a
 * given socket.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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

#include <netinet/in.h>

#include <a3/cpp.h>
#include <a3/types.h>

#include <sc/connection.h>
#include <sc/forward.h>

A3_H_BEGIN

typedef struct ScListener ScListener;

A3_EXPORT ScListener* sc_listener_new(ScFd socket, ScConnectionHandler, ScRouter*, ScEventLoop*);
A3_EXPORT ScListener* sc_listener_tcp_new(in_port_t, ScConnectionHandler, ScRouter*, ScEventLoop*);
A3_EXPORT ScListener* sc_listener_http_new(in_port_t, ScRouter*, ScEventLoop*);
A3_EXPORT void        sc_listener_free(ScListener*);
A3_EXPORT void        sc_listener_start(ScListener*, ScCoMain*);

A3_EXPORT ScRouter* sc_listener_router(ScListener*);

A3_H_END
