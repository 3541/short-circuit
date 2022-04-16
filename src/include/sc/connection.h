/*
 * SHORT CIRCUIT: CONNECTION -- Abstract connection on top of the event
 * interface.
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

#include <a3/buffer.h>
#include <a3/cpp.h>
#include <a3/str.h>
#include <a3/types.h>

#include <sc/forward.h>
#include <sc/io.h>

A3_H_BEGIN

typedef void (*ScConnectionHandler)(ScConnection*);

typedef struct ScConnection {
    A3Buffer send_buf;
    A3Buffer recv_buf;

    struct sockaddr_in6 client_addr;
    socklen_t           addr_len;

    ScCoroutine* coroutine;
    ScListener*  listener;

    ScFd socket;
} ScConnection;

A3_EXPORT void          sc_connection_init(ScConnection*, ScListener*);
A3_EXPORT ScConnection* sc_connection_new(ScListener*);
A3_EXPORT void          sc_connection_destroy(ScConnection*);
A3_EXPORT void          sc_connection_free(void*);

A3_EXPORT void sc_connection_close(ScConnection*);
A3_EXPORT      SC_IO_RESULT(size_t) sc_connection_recv(ScConnection*);
A3_EXPORT SC_IO_RESULT(size_t) sc_connection_recv_until(ScConnection*, A3CString delim, size_t max);

A3_H_END
