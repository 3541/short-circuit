/*
 * SHORT CIRCUIT: CONNECTION -- Abstract connection on top of the event
 * interface.
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

#define _GNU_SOURCE // For SPLICE_F_MORE.

#include "connection.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/util.h>

#include "config.h"
#include "event.h"
#include "forward.h"
#include "http/connection.h"
#include "http/request.h"
#include "http/response.h"
#include "http/types.h"
#include "listen.h"
#include "timeout.h"

#include <liburing/io_uring.h>

static bool connection_timeout_submit(Connection* conn, struct io_uring* uring, time_t delay);

static void connection_recv_handle(EventTarget*, struct io_uring*, void* ctx, bool success,
                                   int32_t status);
static void connection_timeout_handle(Timeout*, struct io_uring*);

static TimeoutQueue connection_timeout_queue;

void connection_timeout_init() { timeout_queue_init(&connection_timeout_queue); }

bool connection_init(Connection* conn) {
    assert(conn);

    A3_TRYB(a3_buf_init(&conn->recv_buf, RECV_BUF_INITIAL_CAPACITY, RECV_BUF_MAX_CAPACITY));
    return a3_buf_init(&conn->send_buf, SEND_BUF_INITIAL_CAPACITY, SEND_BUF_MAX_CAPACITY);
}

bool connection_reset(Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    if (a3_buf_initialized(&conn->recv_buf))
        a3_buf_reset(&conn->recv_buf);
    if (a3_buf_initialized(&conn->recv_buf))
        a3_buf_reset(&conn->send_buf);
    if (timeout_is_scheduled(&conn->timeout)) {
        timeout_cancel(&conn->timeout);
        return connection_timeout_submit(conn, uring, CONNECTION_TIMEOUT);
    }

    return true;
}

static inline void connection_drop(Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    A3_TRACE("Dropping connection.");
    // TODO: Make generic.
    http_connection_free(connection_http(conn), uring);
}

#define CTRYB(CONN, URING, T)                                                                      \
    do {                                                                                           \
        if (!(T)) {                                                                                \
            connection_drop((CONN), (URING));                                                      \
            return;                                                                                \
        }                                                                                          \
    } while (0)

#define CTRYB_MAP(CONN, URING, T, E)                                                               \
    do {                                                                                           \
        if (!(T)) {                                                                                \
            connection_drop((CONN), (URING));                                                      \
            return (E);                                                                            \
        }                                                                                          \
    } while (0)

// Call a connection callback, and close on failure.
static inline void connection_handler_call(Connection* conn, struct io_uring* uring,
                                           ConnectionHandler handler, bool success,
                                           int32_t status) {
    assert(conn);
    assert(uring);
    assert(handler);

    if (handler(conn, uring, success, status))
        return;

    connection_drop(conn, uring);
}

// Handle the completion of an ACCEPT event.
static void connection_accept_handle(EventTarget* target, struct io_uring* uring, void* handler,
                                     bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    A3_TRACE("Accept connection.");
    conn->listener->accept_queued = false;
    if (!success) {
        A3_ERRNO(-status, "accept failed");
        connection_drop(conn, uring);
        return;
    }
    conn->socket = (fd)status;

    CTRYB(conn, uring, connection_timeout_submit(conn, uring, CONNECTION_TIMEOUT));
    CTRYB(conn, uring, connection_recv_submit(conn, uring, handler));
}

static void connection_close_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                    bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    conn->socket = -1;
    if (status < 0)
        A3_ERRNO(-status, "close failed");

    if (ctx)
        connection_handler_call(conn, uring, ctx, success, status);
}

static void connection_recv_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                   bool success, int32_t status) {
    assert(target);
    assert(uring);
    assert(ctx);

    Connection* conn = EVT_PTR(target, Connection);

    if (status == 0 || status == -ECONNRESET) {
        A3_TRACE("Connection closed by peer.");
        connection_drop(conn, uring);
        return;
    }
    if (!success) {
        A3_ERRNO(-status, "recv failed");
        connection_drop(conn, uring);
        return;
    }

    a3_buf_wrote(&conn->recv_buf, (size_t)status);

    connection_handler_call(conn, uring, ctx, success, status);
}

static void connection_send_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                   bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    if (status < 0) {
        A3_ERRNO(-status, "send failed");
        connection_drop(conn, uring);
        return;
    }
    if (!success) {
        A3_ERROR_F("Short send of %d.", status);
        connection_drop(conn, uring);
        return;
    }

    a3_buf_read(&conn->send_buf, (size_t)status);

    if (ctx)
        connection_handler_call(conn, uring, ctx, success, status);
}

static void connection_splice_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                     bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    if (status < 0) {
        A3_ERRNO(-status, "splice failed");
        connection_drop(conn, uring);
        return;
    }
    if (!success) {
        A3_ERROR_F("Short splice out of %d.", status);
        connection_drop(conn, uring);
    }

    if (ctx)
        connection_handler_call(conn, uring, ctx, success, status);
}

static void connection_splice_in_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                        bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    if (status < 0) {
        A3_ERRNO(-status, "splice in failed");
        connection_drop(conn, uring);
        return;
    }
    if (!success)
        A3_WARN_F("Short splice of %d.", status);

    if (ctx) {
        ConnectionSpliceHandler handler = ctx;
        if (!handler(conn, uring, SPLICE_IN, success, status))
            connection_drop(conn, uring);
    }
}

static void connection_splice_out_handle(EventTarget* target, struct io_uring* uring, void* ctx,
                                         bool success, int32_t status) {
    assert(target);
    assert(uring);

    Connection* conn = EVT_PTR(target, Connection);

    if (status < 0) {
        A3_ERRNO(-status, "splice out failed");
        connection_drop(conn, uring);
        return;
    }
    if (!success) {
        A3_ERROR_F("Short splice out of %d.", status);
        connection_drop(conn, uring);
        return;
    }

    if (ctx) {
        ConnectionSpliceHandler handler = ctx;
        if (!handler(conn, uring, SPLICE_OUT, success, status))
            connection_drop(conn, uring);
    }
}

static void connection_timeout_handle(Timeout* timeout, struct io_uring* uring) {
    assert(timeout);
    assert(uring);

    Connection* conn = A3_CONTAINER_OF(timeout, Connection, timeout);

    if (!http_response_error_submit(&connection_http(conn)->response, uring, HTTP_STATUS_TIMEOUT,
                                    HTTP_RESPONSE_CLOSE))
        connection_drop(conn, uring);
}

// Submit an ACCEPT on the uring. The handler given will only be called upon the arrival of input
// data.
Connection* connection_accept_submit(Listener* listener, struct io_uring* uring,
                                     ConnectionHandler handler) {
    assert(listener);
    assert(uring);
    assert(handler);

    // TODO: This needs to be generic over connection types. Perhaps just take as a parameter.
    Connection* ret = (Connection*)http_connection_new();
    if (!ret)
        return NULL;

    ret->listener  = listener;
    ret->transport = listener->transport;
    ret->addr_len  = sizeof(ret->client_addr);

    CTRYB_MAP(ret, uring,
              event_accept_submit(EVT(ret), uring, connection_accept_handle, handler,
                                  listener->socket, &ret->client_addr, &ret->addr_len),
              NULL);

    return ret;
}

// Submit a request to receive as much data as the buffer can handle.
bool connection_recv_submit(Connection* conn, struct io_uring* uring, ConnectionHandler handler) {
    assert(conn);
    assert(uring);
    assert(handler);

    return event_recv_submit(EVT(conn), uring, connection_recv_handle, handler, conn->socket,
                             a3_buf_write_ptr(&conn->recv_buf));
}

bool connection_send_submit(Connection* conn, struct io_uring* uring, ConnectionHandler handler,
                            uint32_t send_flags, uint8_t sqe_flags) {
    assert(conn);
    assert(uring);
    return event_send_submit(EVT(conn), uring, connection_send_handle, handler, conn->socket,
                             a3_buf_read_ptr(&conn->send_buf), send_flags, sqe_flags);
}

#define PIPE_BUF_SIZE 65536ULL

// Wraps splice so it can be used without a pipe.
bool connection_splice_submit(Connection* conn, struct io_uring* uring,
                              ConnectionSpliceHandler splice_handler, ConnectionHandler handler,
                              fd src, size_t file_offset, size_t len, uint8_t sqe_flags) {
    assert(conn);
    assert(uring);
    assert(splice_handler);
    assert(handler);

    if (!conn->pipe[0] && !conn->pipe[1]) {
        A3_TRACE("Opening pipe.");
        if (pipe(conn->pipe) < 0) {
            A3_ERRNO(errno, "unable to open pipe");
            return false;
        }
    }

    for (size_t remaining = len; remaining > 0;) {
        size_t req_len = MIN(PIPE_BUF_SIZE, remaining);
        bool   more    = remaining > PIPE_BUF_SIZE;
        A3_TRYB_MSG(event_splice_submit(EVT(conn), uring, connection_splice_in_handle,
                                        splice_handler, src, len - remaining + file_offset,
                                        conn->pipe[1], req_len, 0, sqe_flags | IOSQE_IO_LINK),
                    A3_LOG_ERROR, "Failed to submit splice in.");
        A3_TRYB_MSG(
            event_splice_submit(EVT(conn), uring,
                                more ? connection_splice_out_handle : connection_splice_handle,
                                more ? (void*)splice_handler : (void*)handler, conn->pipe[0],
                                (uint64_t)-1, conn->socket, req_len, more ? SPLICE_F_MORE : 0,
                                sqe_flags | (more ? IOSQE_IO_LINK : 0)),
            A3_LOG_ERROR, "Failed to submit splice out.");
        if (more)
            remaining -= PIPE_BUF_SIZE;
        else
            remaining = 0;
    }

    return true;
}

// Retry an interrupted splice chain.
bool connection_splice_retry(Connection* conn, struct io_uring* uring,
                             ConnectionSpliceHandler splice_handler, ConnectionHandler handler,
                             fd src, size_t in_buf, size_t file_offset, size_t remaining,
                             uint8_t sqe_flags) {
    assert(conn);
    assert(uring);
    assert(splice_handler);
    assert(handler);

    bool more = remaining > 0;

    // Clear out data currently in the pipe.
    if (in_buf &&
        !event_splice_submit(EVT(conn), uring,
                             more ? connection_splice_out_handle : connection_splice_handle,
                             more ? (void*)splice_handler : (void*)handler, conn->pipe[0],
                             (uint64_t)-1, conn->socket, in_buf, more ? SPLICE_F_MORE : 0,
                             sqe_flags | (more ? IOSQE_IO_LINK : 0))) {
        A3_ERROR("Failed to submit splice.");
        return false;
    }

    // Re-submit the chain.
    if (more)
        return connection_splice_submit(conn, uring, splice_handler, handler, src, file_offset,
                                        remaining, sqe_flags);
    return true;
}

static bool connection_timeout_submit(Connection* conn, struct io_uring* uring, time_t delay) {
    assert(conn);
    assert(uring);

    struct timespec t;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &t));

    conn->timeout.threshold.tv_sec  = t.tv_sec + delay;
    conn->timeout.threshold.tv_nsec = t.tv_nsec;
    conn->timeout.fire              = connection_timeout_handle;
    return timeout_schedule(&connection_timeout_queue, &conn->timeout, uring);
}

bool connection_close_submit(Connection* conn, struct io_uring* uring, ConnectionHandler handler) {
    assert(conn);
    assert(uring);
    assert(handler);

    if (timeout_is_scheduled(&conn->timeout))
        timeout_cancel(&conn->timeout);

    return event_close_submit(EVT(conn), uring, connection_close_handle, (void*)handler,
                              conn->socket, 0, EVENT_FALLBACK_ALLOW);
}
