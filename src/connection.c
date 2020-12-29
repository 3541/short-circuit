#include "connection.h"

#include <assert.h>
#include <errno.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

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

static bool connection_recv_submit(Connection*, struct io_uring*,
                                   uint32_t recv_flags, uint8_t sqe_flags);
static bool connection_timeout_submit(Connection* this, struct io_uring* uring,
                                      time_t delay);

static bool connection_recv_handle(Connection* this, struct io_uring* uring,
                                   int32_t status, bool chain);
static bool connection_timeout_handle(Timeout*, struct io_uring*);

static TimeoutQueue connection_timeout_queue;

void connection_timeout_init() {
    timeout_queue_init(&connection_timeout_queue);
}

bool connection_init(Connection* this) {
    assert(this);

    TRYB(buf_init(&this->recv_buf, RECV_BUF_INITIAL_CAPACITY,
                  RECV_BUF_MAX_CAPACITY));
    return buf_init(&this->send_buf, SEND_BUF_INITIAL_CAPACITY,
                    SEND_BUF_MAX_CAPACITY);
}

bool connection_reset(Connection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (buf_initialized(&this->recv_buf))
        buf_reset(&this->recv_buf);
    if (buf_initialized(&this->recv_buf))
        buf_reset(&this->send_buf);
    if (timeout_is_scheduled(&this->timeout)) {
        timeout_cancel(&this->timeout);
        return connection_timeout_submit(this, uring, CONNECTION_TIMEOUT);
    }

    return true;
}

// Submit an ACCEPT on the uring.
Connection* connection_accept_submit(Listener*        listener,
                                     struct io_uring* uring) {
    assert(listener);
    assert(uring);

    Connection* ret = (Connection*)http_connection_new();
    if (!ret)
        return NULL;

    ret->listener = listener;

    ret->transport = listener->transport;
    switch (ret->transport) {
    case PLAIN:
        ret->recv_submit = connection_recv_submit;
        ret->recv_handle = connection_recv_handle;

        ret->send_submit = connection_send_submit;
        break;
    case TLS:
        PANIC("TODO: TLS");
    default:
        PANIC("Invalid transport.");
    }

    ret->addr_len = sizeof(ret->client_addr);

    if (!event_accept_submit(EVT(ret), uring, listener->socket,
                             &ret->client_addr, &ret->addr_len)) {
        http_connection_free((HttpConnection*)ret, uring);
        return NULL;
    }

    return ret;
}

// Submit a request to receive as much data as the buffer can handle.
static bool connection_recv_submit(Connection* this, struct io_uring* uring,
                                   uint32_t recv_flags, uint8_t sqe_flags) {
    assert(this);
    assert(uring);
    (void)recv_flags;
    (void)sqe_flags;

    return event_recv_submit(EVT(this), uring, this->socket,
                             buf_write_ptr(&this->recv_buf));
}

bool connection_send_submit(Connection* this, struct io_uring* uring,
                            uint32_t send_flags, uint8_t sqe_flags) {
    assert(this);
    assert(uring);

    Buffer* buf = &this->send_buf;
    return event_send_submit(EVT(this), uring, this->socket, buf_read_ptr(buf),
                             send_flags, sqe_flags);
}

// Wraps splice so it can be used without a pipe.
bool connection_splice_submit(Connection* this, struct io_uring* uring, fd src,
                              size_t len, uint8_t sqe_flags) {
    assert(this);
    assert(uring);

    fd pipefd[2];
    UNWRAPSD(pipe(pipefd));

    if (!event_splice_submit(EVT(this), uring, src, pipefd[1], len,
                             sqe_flags | IOSQE_IO_LINK, true) ||
        !event_splice_submit(EVT(this), uring, pipefd[0], this->socket, len,
                             sqe_flags | IOSQE_IO_LINK, false)) {
        ERR("Failed to submit splice.");
        goto fail;
    }

    TRYB(event_close_submit(NULL, uring, pipefd[0], sqe_flags | IOSQE_IO_LINK,
                            EVENT_FALLBACK_FORBID));
    return event_close_submit(NULL, uring, pipefd[1], sqe_flags,
                              EVENT_FALLBACK_FORBID);

fail:
    UNWRAPSD(close(pipefd[0]));
    UNWRAPSD(close(pipefd[1]));
    return false;
}

static bool connection_timeout_submit(Connection* this, struct io_uring* uring,
                                      time_t delay) {
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

    return event_close_submit(EVT(this), uring, this->socket, 0,
                              EVENT_FALLBACK_FORBID);
}

// Handle the completion of an ACCEPT event.
static bool connection_accept_handle(Connection* this, struct io_uring* uring,
                                     int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    log_msg(TRACE, "Accept connection.");
    this->listener->accept_queued = false;
    if (status < 0) {
        log_error(-status, "accept failed");
        return false;
    }
    this->socket = (fd)status;

    TRYB(connection_timeout_submit(this, uring, CONNECTION_TIMEOUT));

    return this->recv_submit(this, uring, 0, 0);
}

static bool connection_close_handle(Connection* this, struct io_uring* uring,
                                    int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    if (status >= 0)
        this->socket = -1;
    else
        log_error(-status, "close failed");
    http_connection_free((HttpConnection*)this, uring);

    return true;
}

static bool connection_openat_handle(Connection* this, struct io_uring* uring,
                                     int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    fd file = -3;
    if (status < 0) {
        // Signal to later handlers that the open failed. Not sure if it's
        // better to check for more specific errors here. As it is currently,
        // any failure to open a file is going to be exposed as a 404,
        // regardless of the actual cause.

        log_error(-status, "openat failed");
        file = -2;
    } else {
        file = status;
    }

    // FIXME: This is ugly and unnecessary coupling.
    HttpConnection* conn = (HttpConnection*)this;
    conn->target_file = file;

    return http_response_handle(conn, uring);
}

static bool connection_read_handle(Connection* this, struct io_uring* uring,
                                   int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(chain);
    (void)uring;
    (void)chain;

    if (status < 0) {
        log_error(-status, "read failed");
        return false;
    }

    buf_wrote(&this->send_buf, (size_t)status);
    return true;
}

static bool connection_recv_handle(Connection* this, struct io_uring* uring,
                                   int32_t status, bool chain) {
    assert(this);
    assert(uring);
    assert(!chain);
    (void)chain;

    if (status == 0 || -status == ECONNRESET)
        return connection_close_submit(this, uring);
    if (status < 0) {
        log_error(-status, "recv failed");
        return false;
    }

    buf_wrote(&this->recv_buf, (size_t)status);

    HttpRequestResult rc = http_request_handle((HttpConnection*)this, uring);
    if (rc == HTTP_REQUEST_ERROR) {
        return false;
    } else if (rc == HTTP_REQUEST_NEED_DATA) {
        // Need more data.
        return this->recv_submit(this, uring, 0, 0);
    }

    return true;
}

static bool connection_send_handle(Connection* this, struct io_uring* uring,
                                   int32_t status, bool chain) {
    assert(this);
    assert(uring);

    if (status < 0) {
        log_error(-status, "send failed");
        return false;
    }

    buf_read(&this->send_buf, (size_t)status);

    if (chain)
        return true;

    return http_response_handle((HttpConnection*)this, uring);
}

static bool connection_splice_handle(Connection* this, struct io_uring* uring,
                                     int32_t status, bool chain) {
    assert(this);
    assert(uring);
    (void)chain;
    // Splice with chain but without ignore must be handled, since a splice call
    // will nearly always be in a chain with the closure of a pipe.

    if (status < 0) {
        log_error(-status, "splice failed");
        return false;
    }

    return http_response_handle((HttpConnection*)this, uring);
}

static bool connection_timeout_handle(Timeout*         timeout,
                                      struct io_uring* uring) {
    assert(timeout);
    assert(uring);

    Connection* this =
        (Connection*)((uintptr_t)timeout - offsetof(Connection, timeout));
    assert(this);

    return http_response_error_submit((HttpConnection*)this, uring,
                                      HTTP_STATUS_TIMEOUT, HTTP_RESPONSE_CLOSE);
}

void connection_event_handle(Connection* conn, struct io_uring* uring,
                             EventType type, int32_t status, bool chain) {
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
    case EVENT_OPENAT:
        rc = connection_openat_handle(conn, uring, status, chain);
        break;
    case EVENT_READ:
        rc = connection_read_handle(conn, uring, status, chain);
        break;
    case EVENT_RECV:
        rc = connection_recv_handle(conn, uring, status, chain);
        break;
    case EVENT_SEND:
        rc = connection_send_handle(conn, uring, status, chain);
        break;
    case EVENT_SPLICE:
        rc = connection_splice_handle(conn, uring, status, chain);
        break;
    case EVENT_CANCEL:
    case EVENT_TIMEOUT:
    case EVENT_INVALID:
        PANIC_FMT("Invalid event %d.", type);
    }

    if (!rc) {
        log_msg(WARN, "Connection failure. Closing.");
        http_connection_free((HttpConnection*)conn, uring);
    }
}
