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

#include "listen.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <a3/buffer.h>
#include <a3/log.h>

#include <sc/connection.h>
#include <sc/forward.h>
#include <sc/io.h>
#include <sc/listen.h>

#include "config.h"
#include "connection.h"
#include "proto/http/connection.h"

ScListener* sc_listener_new(ScFd socket, ScConnectionHandler connection_handler, ScRouter* router) {
    assert(socket >= 0);
    assert(connection_handler);

    A3_UNWRAPNI(ScListener*, ret, calloc(1, sizeof(*ret)));
    *ret = (ScListener) {
        .connection_handler = connection_handler,
        .socket             = socket,
        .router             = router,
    };

    return ret;
}

ScListener* sc_listener_tcp_new(in_port_t port, ScConnectionHandler connection_handler,
                                ScRouter* router) {
    assert(port);
    assert(connection_handler);

    ScFd sock = -1;
    A3_UNWRAPS(sock, socket(AF_INET6, SOCK_STREAM, 0));

    A3_UNWRAPSD(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &(int) { 1 }, sizeof(int)));
    // TODO: Doesn't work on OpenBSD.
    A3_UNWRAPSD(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &(int) { 0 }, sizeof(int)));

#ifdef SC_IO_BACKEND_POLL
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        A3_ERRNO(errno, "GETFL");
        A3_PANIC("GETFL failed");
    }
    A3_UNWRAPSD(fcntl(sock, F_SETFL, flags | O_NONBLOCK));
#endif

    struct sockaddr_in6 addr = { .sin6_family = AF_INET6,
                                 .sin6_port   = htons(port),
                                 .sin6_addr   = IN6ADDR_ANY_INIT };
    A3_UNWRAPSD(bind(sock, (struct sockaddr*)&addr, sizeof(addr)));
    A3_UNWRAPSD(listen(sock, SC_LISTEN_BACKLOG));

    return sc_listener_new(sock, connection_handler, router);
}

ScListener* sc_listener_http_new(in_port_t port, ScRouter* router) {
    assert(router);

    ScListener* ret = sc_listener_tcp_new(port, sc_http_connection_handle, router);

    return ret;
}

void sc_listener_free(ScListener* listener) {
    assert(listener);

    if (listener->socket >= 0)
        A3_UNWRAPSD(close(listener->socket));

    if (listener->router)
        sc_router_free(listener->router);

    free(listener);
}

static ssize_t sc_listen(void* data) {
    assert(data);

    ScListener* listener = data;

    while (true) {
        ScConnection* conn = sc_connection_new(listener);

        conn->socket = SC_IO_UNWRAP(sc_io_accept(
            listener->socket, (struct sockaddr*)(&conn->client_addr), &conn->addr_len));
        A3_TRACE("Accepted connection.");

        ScCoroutine* co = sc_co_spawn(sc_connection_handle, conn);
        sc_co_defer_on(co, sc_connection_free, conn);
    }
}

void sc_listener_start(ScListener* listener, ScCoMain* co_main) {
    assert(listener);
    assert(co_main);

    A3_TRACE("Starting listener coroutine.");
    ScCoroutine* co = sc_co_new(co_main, sc_listen, listener);
    sc_co_resume(co, 0);
}

ScRouter* sc_listener_router(ScListener* listener) {
    assert(listener && listener->router);

    return listener->router;
}
