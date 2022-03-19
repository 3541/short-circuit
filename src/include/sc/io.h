/*
 * SHORT CIRCUIT: IO -- Primitive IO operations.
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
 *
 * Formerly known as event.h.
 */

#pragma once

#include <netinet/in.h>

#include <a3/str.h>

#include <sc/forward.h>

ScEventLoop* sc_io_event_loop_new(void);
void         sc_io_event_loop_run(ScEventLoop*);
void         sc_io_event_loop_pump(ScEventLoop*);
void         sc_io_event_loop_free(ScEventLoop*);

ScIoFuture* sc_io_accept(ScEventLoop*, ScCoroutine*, ScFd sock, struct sockaddr* client_addr,
                         socklen_t* addr_len);
ScIoFuture* sc_io_recv(ScEventLoop*, ScCoroutine*, ScFd sock, A3String dst);
ScIoFuture* sc_io_openat(ScEventLoop*, ScCoroutine*, ScFd dir, A3CString path, int open_flags);
ScIoFuture* sc_io_read(ScEventLoop*, ScCoroutine*, ScFd, A3String dst, off_t);
