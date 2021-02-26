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
#include <liburing/io_uring.h>
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

static bool connection_recv_submit(Connection*, struct io_uring*, uint32_t recv_flags,
                                   uint8_t sqe_flags);
static bool connection_timeout_submit(Connection* this, struct io_uring* uring, time_t delay);

static bool connection_recv_handle(Connection* this, struct io_uring* uring, int32_t status,
                                   bool chain);
static bool connection_timeout_handle(Timeout*, struct io_uring*);

static TimeoutQueue connection_timeout_queue;

static inline HttpConnection* connection_http(Connection* conn) {
    assert(conn);

    return A3_CONTAINER_OF(conn, HttpConnection, conn);
}

void connection_timeout_init() { timeout_queue_init(&connection_timeout_queue); }

bool connection_init(Connection* this) {
    assert(this);

    A3_TRYB(a3_buf_init(&this->recv_buf, RECV_BUF_INITIAL_CAPACITY, RECV_BUF_MAX_CAPACITY));
    return a3_buf_init(&this->send_buf, SEND_BUF_INITIAL_CAPACITY, SEND_BUF_MAX_CAPACITY);
}

bool connection_reset(Connection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (a3_buf_initialized(&this->recv_buf))
        a3_buf_reset(&this->recv_buf);
    if (a3_buf_initialized(&this->recv_buf))
        a3_buf_reset(&this->send_buf);
    if (timeout_is_scheduled(&this->timeout)) {
        timeout_cancel(&this->timeout);
        return connection_timeout_submit(this, uring, CONNECTION_TIMEOUT);
    }

    return true;
}

// Submit an ACCEPT on the uring.
Connection* connection_accept_submit(Listener* listener, struct io_uring* uring) {
    assert(listener);
    assert(uring);

    Connection* ret = (Connection*)http_connection_new();
    if (!ret)
        return NULL;

    ret->listener = listener;

    ret->transport = listener->transport;
    switch (ret->transport) {
    case TRANSPORT_PLAIN:
        ret->recv_submit = connection_recv_submit;
        ret->recv_handle = connection_recv_handle;

        ret->send_submit = connection_send_submit;
        break;
    case TRANSPORT_TLS:
        A3_PANIC("TODO: TLS");
    default:
        A3_PANIC("Invalid transport.");
    }

    ret->addr_len = sizeof(ret->client_addr);

    if (!event_accept_submit(EVT(ret), uring, listener->socket, &ret->client_addr,
                             &ret->addr_len)) {
        http_connection_free((HttpConnection*)ret, uring);
        return NULL;
    }

    return ret;
}

// Submit a request to receive as much data as the buffer can handle.
static bool connection_recv_submit(Connection* this, struct io_uring* uring, uint32_t recv_flags,
                                   uint8_t sqe_flags) {
    assert(this);
    assert(uring);
    (void)recv_flags;
    (void)sqe_flags;

    return event_recv_submit(EVT(this), uring, this->socket, a3_buf_write_ptr(&this->recv_buf));
}

bool connection_send_submit(Connection* this, struct io_uring* uring, uint32_t send_flags,
                            uint8_t sqe_flags) {
    assert(this);
    assert(uring);

    A3Buffer* buf = &this->send_buf;
    return event_send_submit(EVT(this), uring, this->socket, a3_buf_read_ptr(buf), send_flags,
                             sqe_flags);
}

#define PIPE_BUF_SIZE 65536ULL

// Wraps splice so it can be used without a pipe.
bool connection_splice_submit(Connection* this, struct io_uring* uring, fd src, size_t len,
                              uint8_t sqe_flags) {
    assert(this);
    assert(uring);

    if (!this->pipe[0] && !this->pipe[1]) {
        a3_log_msg(LOG_TRACE, "Opening pipe.");
        if (pipe(this->pipe) < 0) {
            a3_log_error(errno, "unable to open pipe");
            return false;
        }
    }

    // TODO: This is horrendous. Clean it up.
    for (size_t sent = 0, remaining = len, to_send      = MIN(PIPE_BUF_SIZE, remaining); sent < len;
         sent += to_send, remaining -= to_send, to_send = MIN(PIPE_BUF_SIZE, remaining)) {
        if (!event_splice_submit(EVT(this), uring, src, sent, this->pipe[1], to_send, 0,
                                 sqe_flags | IOSQE_IO_LINK, false) ||
            !event_splice_submit(EVT(this), uring, this->pipe[0], (uint64_t)-1, this->socket,
                                 to_send, (remaining > PIPE_BUF_SIZE) ? SPLICE_F_MORE : 0,
                                 sqe_flags | ((remaining > PIPE_BUF_SIZE) ? IOSQE_IO_LINK : 0),
                                 (remaining > PIPE_BUF_SIZE) ? false : true)) {
            A3_ERR("Failed to submit splice.");
            return false;
        }
    }

    return true;
}

static bool connection_timeout_submit(Connection* this, struct io_uring* uring, time_t delay) {
    assert(this);
    assert(uring);

    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) < 0)
        return false;

    this->timeout.threshold.tv_sec  = t.tv_sec + delay;
    this->timeout.threshold.tv_nsec = t.tv_nsec;
    this->timeout.fire              = connection_timeout_handle;
    return timeout_schedule(&connection_timeout_queue, &this->timeout, uring);
}

bool connection_close_submit(Connection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (timeout_is_scheduled(&this->timeout))
        timeout_cancel(&this->timeout);

    return event_close_submit(EVT(this), uring, this->socket, 0, EVENT_FALLBACK_FORBID);
}

// Handle the completion of an ACCEPT event.
static bool connection_accept_handle(Connection* this, struct io_uring* uring, int32_t status,
                                     bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    a3_log_msg(LOG_TRACE, "Accept connection.");
    this->listener->accept_queued = false;
    if (status < 0) {
        a3_log_error(-status, "accept failed");
        return false;
    }
    this->socket = (fd)status;

    A3_TRYB(connection_timeout_submit(this, uring, CONNECTION_TIMEOUT));

    return this->recv_submit(this, uring, 0, 0);
}

static bool connection_close_handle(Connection* this, struct io_uring* uring, int32_t status,
                                    bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    this->socket = -1;
    if (status < 0)
        a3_log_error(-status, "close failed");
    http_connection_free((HttpConnection*)this, uring);

    return true;
}

static bool connection_openat_handle(Connection* conn, struct io_uring* uring, int32_t status,
                                     bool chain) {
    assert(conn);
    assert(uring);
    assert(!chain);
    (void)chain;
    (void)status;

    // FIXME: This is ugly and unnecessary coupling.
    return http_response_handle((HttpConnection*)conn, uring);
}

static bool connection_read_handle(Connection* this, struct io_uring* uring, bool success,
                                   int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(chain);
    (void)uring;
    (void)chain;

    if (status < 0) {
        a3_log_error(-status, "read failed");
        return false;
    }
    if (!success) {
        // TODO: Handle short reads.
        a3_log_fmt(LOG_ERROR, "Short read of %d.", status);
        return false;
    }

    a3_buf_wrote(&this->send_buf, (size_t)status);
    return true;
}

static bool connection_recv_handle(Connection* this, struct io_uring* uring, int32_t status,
                                   bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    if (status == 0 || status == -ECONNRESET)
        return connection_close_submit(this, uring);
    if (status < 0) {
        a3_log_error(-status, "recv failed");
        return false;
    }

    a3_buf_wrote(&this->recv_buf, (size_t)status);

    HttpRequestResult rc = http_request_handle((HttpConnection*)this, uring);
    if (rc == HTTP_REQUEST_ERROR)
        return false;
    else if (rc == HTTP_REQUEST_NEED_DATA)
        return this->recv_submit(this, uring, 0, 0);

    return true;
}

static bool connection_send_handle(Connection* this, struct io_uring* uring, bool success,
                                   int32_t status, bool chain) {
    assert(this);
    assert(uring);

    if (status < 0) {
        a3_log_error(-status, "send failed");
        return false;
    }
    if (!success) {
        a3_log_fmt(LOG_ERROR, "Short send of %d.", status);
        return false;
    }

    a3_buf_read(&this->send_buf, (size_t)status);

    if (chain)
        return true;

    return http_response_handle((HttpConnection*)this, uring);
}

static bool connection_splice_handle(Connection* this, struct io_uring* uring, bool success,
                                     int32_t status, bool chain) {
    assert(this);
    assert(uring);

    if (status < 0) {
        a3_log_error(-status, "splice failed");
        return false;
    }

    if (chain && success)
        return true;

    return http_response_splice_handle((HttpConnection*)this, uring, success, status);
}

static bool connection_timeout_handle(Timeout* timeout, struct io_uring* uring) {
    assert(timeout);
    assert(uring);

    Connection* conn = A3_CONTAINER_OF(timeout, Connection, timeout);

    return http_response_error_submit(&connection_http(conn)->response, uring, HTTP_STATUS_TIMEOUT,
                                      HTTP_RESPONSE_CLOSE);
}

void connection_event_handle(Connection* conn, struct io_uring* uring, EventType type, bool success,
                             int32_t status, bool chain) {
    assert(conn);
    assert(uring);

    bool rc = true;

    switch (type) {
    case EVENT_ACCEPT:
        rc = connection_accept_handle(conn, uring, status, chain);
        break;
    case EVENT_CLOSE:
        rc = connection_close_handle(conn, uring, status, chain);
        break;
    case EVENT_OPENAT_SYNTH:
        rc = connection_openat_handle(conn, uring, status, chain);
        break;
    case EVENT_READ:
        rc = connection_read_handle(conn, uring, success, status, chain);
        break;
    case EVENT_RECV:
        rc = conn->recv_handle(conn, uring, status, chain);
        break;
    case EVENT_SEND:
        rc = connection_send_handle(conn, uring, success, status, chain);
        break;
    case EVENT_SPLICE:
        rc = connection_splice_handle(conn, uring, success, status, chain);
        break;
    case EVENT_INVALID:
    case EVENT_OPENAT:
    case EVENT_STAT:
    case EVENT_TIMEOUT:
        A3_PANIC_FMT("Invalid event %d.", type);
    }

    if (!rc) {
        a3_log_msg(LOG_WARN, "Connection failure. Closing.");
        http_connection_free((HttpConnection*)conn, uring);
    }
}
