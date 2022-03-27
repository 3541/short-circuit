/*
 * SHORT CIRCUIT: CONNECTION -- Abstract connection.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#include "connection.h"

#include <assert.h>

#include <a3/buffer.h>
#include <a3/log.h>

#include <sc/coroutine.h>
#include <sc/io.h>

#include "config.h"
#include "listen.h"

ssize_t sc_connection_handle(ScCoroutine* self, void* data) {
    assert(self);
    assert(data);

    A3_TRACE("Handling connection.");
    ScConnection* conn = data;

    ssize_t res = sc_connection_recv(conn);
    if (res < 0) {
        A3_ERRNO((int)-res, "recv failed");
        return res;
    }

    conn->listener->connection_handler(conn);

    return 0;
}

void sc_connection_init(ScConnection* conn, ScListener* listener) {
    assert(conn);
    assert(listener);

    *conn = (ScConnection) {
        .addr_len  = sizeof(conn->client_addr),
        .coroutine = NULL,
        .listener  = listener,
        .socket    = -1,
    };

    a3_buf_init(&conn->send_buf, SC_SEND_BUF_INIT_CAP, SC_SEND_BUF_MAX_CAP);
    a3_buf_init(&conn->recv_buf, SC_RECV_BUF_INIT_CAP, SC_RECV_BUF_MAX_CAP);
}

ScConnection* sc_connection_new(ScListener* listener) {
    assert(listener);

    A3_UNWRAPNI(ScConnection*, ret, calloc(1, sizeof(*ret)));

    sc_connection_init(ret, listener);

    return ret;
}

void sc_connection_destroy(ScConnection* conn) {
    assert(conn);

    a3_buf_destroy(&conn->send_buf);
    a3_buf_destroy(&conn->recv_buf);

    if (conn->socket >= 0)
        sc_io_close(conn->coroutine, conn->socket);
}

void sc_connection_free(void* conn) {
    assert(conn);

    sc_connection_destroy(conn);
    free(conn);
}

ssize_t sc_connection_recv(ScConnection* conn) {
    assert(conn);

    a3_buf_ensure_cap(&conn->recv_buf, SC_RECV_BUF_MIN_SPACE);

    return sc_io_recv(conn->coroutine, conn->socket, a3_buf_write_ptr(&conn->recv_buf));
}
