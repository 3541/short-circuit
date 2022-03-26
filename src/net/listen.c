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

#include <assert.h>
#include <sys/socket.h>

#include <a3/buffer.h>
#include <a3/log.h>

#include <sc/connection.h>
#include <sc/coroutine.h>
#include <sc/io.h>
#include <sc/listen.h>

#include "config.h"
#include "connection.h"
#include "listen.h"
#include "proto/http/request.h"

ScListener* sc_listener_new(ScFd socket, ScListenHandler handler,
                            ScConnectionHandler connection_handler) {
    assert(socket >= 0);
    assert(handler);
    assert(connection_handler);

    A3_UNWRAPNI(ScListener*, ret, calloc(1, sizeof(*ret)));
    *ret = (ScListener) {
        .handler            = handler,
        .connection_handler = connection_handler,
        .socket             = socket,
        .data               = NULL,
    };

    return ret;
}

ScListener* sc_listener_tcp_new(in_port_t port, ScConnectionHandler handler) {
    assert(handler);

    ScFd sock = -1;
    A3_UNWRAPS(sock, socket(AF_INET6, SOCK_STREAM, 0));

    int const enable = 1;
    A3_UNWRAPSD(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));

    struct sockaddr_in6 addr = { .sin6_family = AF_INET6,
                                 .sin6_port   = htons(port),
                                 .sin6_addr   = IN6ADDR_ANY_INIT };

    A3_UNWRAPSD(bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    A3_UNWRAPSD(listen(sock, SC_LISTEN_BACKLOG));

    return sc_listener_new(sock, sc_connection_new, handler);
}

ScListener* sc_listener_http_new(in_port_t port, ScHttpRequestHandler handler) {
    assert(handler);

    ScListener* ret = sc_listener_tcp_new(port, sc_http_request_handle);
    ret->data       = handler;

    return ret;
}

static ssize_t sc_listen(ScCoroutine* self, void* data) {
    assert(self);
    assert(data);

    ScListener* listener = data;

    while (true) {
        ScConnection* conn = listener->handler(listener);
        conn->coroutine    = sc_co_spawn(self, sc_connection_handle, conn);

        ScFd res = sc_io_accept(self, listener->socket, (struct sockaddr*)(&conn->client_addr),
                                &conn->addr_len);
        if (res < 0) {
            A3_ERRNO(-res, "accept failed");
            abort();
        }
        A3_TRACE("Accepted connection.");

        sc_co_resume(conn->coroutine, 0);
    }
}

void sc_listener_start(ScListener* listener, ScCoCtx* caller, ScEventLoop* ev) {
    assert(listener);
    assert(caller);
    assert(ev);

    A3_TRACE("Starting listener coroutine.");
    ScCoroutine* co = sc_co_new(caller, ev, sc_listen, listener);
    sc_co_resume(co, 0);
}
