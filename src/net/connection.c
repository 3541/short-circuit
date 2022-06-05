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
#include <sc/timeout.h>

#include "config.h"
#include "io/io.h"
#include "listen.h"

ssize_t sc_connection_handle(void* data) {
    assert(data);

    A3_TRACE("Handling connection.");
    ScConnection* conn = data;

    sc_connection_timeout_reset(conn);

    SC_IO_RESULT(size_t) res = sc_connection_recv(conn);
    if (SC_IO_IS_ERR(res)) {
        if (res.err != SC_IO_EOF)
            SC_IO_UNWRAP(res);
        return 0;
    }
    conn->listener->connection_handler(conn);

    return 0;
}

static void sc_connection_time_out(ScTimeout* timeout) {
    assert(timeout);

    A3_TRACE("Connection timed out.");

    ScConnection* conn = A3_CONTAINER_OF(timeout, ScConnection, timeout);
    sc_co_resume(conn->coroutine, SC_IO_TIMED_OUT);
}

void sc_connection_init(ScConnection* conn, ScListener* listener) {
    assert(conn);
    assert(listener);

    *conn = (ScConnection) {
        .addr_len = sizeof(conn->client_addr),
        .listener = listener,
        .socket   = -1,
    };
    if (listener->timer)
        sc_timeout_init(&conn->timeout, sc_connection_time_out, listener->connection_timeout_s);

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

    A3_TRACE("Destroying connection.");

    a3_buf_destroy(&conn->send_buf);
    a3_buf_destroy(&conn->recv_buf);

    sc_timeout_cancel(&conn->timeout);

    sc_connection_close(conn);
}

void sc_connection_free(void* conn) {
    assert(conn);

    sc_connection_destroy(conn);
    free(conn);
}

void sc_connection_close(ScConnection* conn) {
    assert(conn);

    if (conn->socket < 0)
        return;

    SC_IO_UNWRAP(sc_io_close(conn->socket));
    conn->socket = -1;
}

SC_IO_RESULT(size_t) sc_connection_recv(ScConnection* conn) {
    assert(conn);

    a3_buf_ensure_cap(&conn->recv_buf, SC_RECV_BUF_MIN_SPACE);

    size_t res = SC_IO_TRY(size_t, sc_io_recv(conn->socket, a3_buf_write_ptr(&conn->recv_buf)));
    assert(res <= a3_buf_space(&conn->recv_buf));
    a3_buf_wrote(&conn->recv_buf, res);

    return SC_IO_OK(size_t, res);
}

SC_IO_RESULT(size_t) sc_connection_recv_until(ScConnection* conn, A3CString delim, size_t max) {
    assert(conn);
    assert(delim.ptr && *delim.ptr);

    A3Buffer* buf      = &conn->recv_buf;
    size_t    prev_len = a3_buf_len(buf);
    while (!a3_buf_memmem(buf, delim).ptr && a3_buf_len(buf) <= max) {
        a3_buf_ensure_cap(&conn->recv_buf, SC_RECV_BUF_MIN_SPACE);
        SC_IO_TRY(size_t, sc_connection_recv(conn));
    }

    return SC_IO_OK(size_t, a3_buf_len(buf) - prev_len);
}

void sc_connection_timeout_reset(ScConnection* conn) {
    assert(conn);

    sc_timeout_reset(&conn->timeout);
}

void sc_connection_timeout_arm(ScConnection* conn, ScTimer* timer) {
    assert(conn);
    assert(timer);

    if (!A3_LL_NEXT(&conn->timeout, link))
        sc_timeout_add(timer, &conn->timeout);
    sc_connection_timeout_reset(conn);
}
