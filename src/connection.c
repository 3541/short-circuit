#include "connection.h"

#include <assert.h>
#include <errno.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>

#include "buffer.h"
#include "config.h"
#include "event.h"
#include "forward.h"
#include "http/connection.h"
#include "http/request.h"
#include "http/response.h"
#include "http/types.h"
#include "listen.h"
#include "log.h"
#include "util.h"

static bool connection_recv_submit(Connection* this, struct io_uring* uring,
                                   unsigned sqe_flags);
static bool connection_recv_handle(Connection* this, struct io_uring_cqe* cqe,
                                   struct io_uring* uring);

bool connection_init(Connection* this) {
    assert(this);

    TRYB(buf_init(&this->recv_buf, RECV_BUF_INITIAL_CAPACITY,
                  RECV_BUF_MAX_CAPACITY));
    return buf_init(&this->send_buf, SEND_BUF_INITIAL_CAPACITY,
                    SEND_BUF_MAX_CAPACITY);
}

void connection_reset(Connection* this) {
    assert(this);

    buf_reset(&this->recv_buf);
    buf_reset(&this->send_buf);
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

    if (!event_accept_submit(&ret->last_event, uring, listener->socket,
                             &ret->client_addr, &ret->addr_len)) {
        http_connection_free((HttpConnection*)ret, uring);
        return NULL;
    }

    return ret;
}

// Submit a request to receive as much data as the buffer can handle.
static bool connection_recv_submit(Connection* this, struct io_uring* uring,
                                   unsigned sqe_flags) {
    assert(this);
    assert(uring);
    (void)sqe_flags;

    return event_recv_submit(&this->last_event, uring, this->socket,
                             buf_write_ptr(&this->recv_buf));
}

bool connection_send_submit(Connection* this, struct io_uring* uring,
                            unsigned sqe_flags) {
    assert(this);
    assert(uring);

    Buffer* buf = &this->send_buf;
    return event_send_submit(&this->last_event, uring, this->socket,
                             buf_read_ptr(buf), sqe_flags);
}

bool connection_close_submit(Connection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    return event_close_submit(&this->last_event, uring, this->socket);
}

// Handle the completion of an ACCEPT event.
static bool connection_accept_handle(Connection* this, struct io_uring_cqe* cqe,
                                     struct io_uring* uring) {
    assert(this);
    assert(cqe);
    assert(uring);

    this->listener->accept_queued = false;

    log_msg(TRACE, "Accept connection.");

    this->socket = cqe->res;

    return this->recv_submit(this, uring, 0);
}

static bool connection_recv_handle(Connection* this, struct io_uring_cqe* cqe,
                                   struct io_uring* uring) {
    assert(this);
    assert(cqe);
    assert(uring);

    if (cqe->res == 0)
        return connection_close_submit(this, uring);

    buf_wrote(&this->recv_buf, cqe->res);

    HttpRequestResult rc = http_request_handle((HttpConnection*)this, uring);
    if (rc == HTTP_REQUEST_ERROR) {
        return false;
    } else if (rc == HTTP_REQUEST_NEED_DATA) {
        // Need more data.
        return this->recv_submit(this, uring, 0);
    }

    return true;
}

static bool connection_send_handle(Connection* this, struct io_uring_cqe* cqe,
                                   struct io_uring* uring) {
    assert(this);
    assert(uring);
    assert(cqe);

    buf_read(&this->send_buf, cqe->res);

    return http_response_handle((HttpConnection*)this, uring);
}

static void connection_close_handle(Connection* this, struct io_uring_cqe* cqe,
                                    struct io_uring* uring) {
    assert(this);
    assert(this->last_event.type == CLOSE);
    assert(cqe);
    assert(uring);
    (void)cqe;

    http_connection_free((HttpConnection*)this, uring);
}

// Dispatch an event pertaining to a connection. Returns false to die.
bool connection_event_dispatch(Connection* this, struct io_uring_cqe* cqe,
                               struct io_uring* uring) {
    assert(this);
    assert(cqe);
    assert(uring);

    if (cqe->res < 0) {
        // EOF conditions.
        if (-cqe->res != ECONNRESET && -cqe->res != EBADF)
            log_error(-cqe->res, "Event error. Closing connection.");
        http_connection_free((HttpConnection*)this, uring);
        return true;
    }

    bool rc = true;

    switch (this->last_event.type) {
    case ACCEPT:
        rc = connection_accept_handle(this, cqe, uring);
        break;
    case SEND:
        rc = connection_send_handle(this, cqe, uring);
        break;
    case RECV:
        rc = this->recv_handle(this, cqe, uring);
        break;
    case CLOSE:
        connection_close_handle(this, cqe, uring);
        break;
    case TIMEOUT:
    case INVALID_EVENT:
        ERR("Got invalid event.");
        return false;
    }

    // Unrecoverable connection error. Clean this one up.
    if (!rc) {
        ERR_FMT("Connection error (%s). Dropping.",
                event_type_name(this->last_event.type));
        http_connection_free((HttpConnection*)this, uring);
    }

    return true;
}
