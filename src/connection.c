#include "connection.h"

#include <errno.h>
#include <fcntl.h>
#include <liburing.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"
#include "config.h"
#include "event.h"
#include "http_connection.h"
#include "http_request.h"
#include "http_response.h"
#include "listen.h"
#include "log.h"
#include "socket.h"
#include "util.h"

void connection_reset(Connection* this) {
    assert(this);

    if (buf_initialized(&this->recv_buf))
        buf_reset(&this->recv_buf);
    if (buf_initialized(&this->send_buf))
        buf_reset(&this->send_buf);
}

bool connection_send_submit(Connection* this, struct io_uring* uring,
                            unsigned sqe_flags) {
    assert(this);
    assert(uring);

    Buffer* buf = &this->send_buf;
    return event_send_submit(&this->last_event, uring, this->socket,
                             buf_read_ptr(buf), sqe_flags);
}

static bool connection_send_handle(Connection* this, struct io_uring_cqe* cqe,
                                   struct io_uring* uring) {
    assert(this);
    assert(uring);
    assert(cqe);

    if (cqe->res < 0) {
        log_error(-cqe->res, "SEND");
        return false;
    }

    buf_read(&this->send_buf, cqe->res);

    return http_response_handle((HttpConnection*)this, uring);
}

bool connection_close_submit(Connection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    return event_close_submit(&this->last_event, uring, this->socket);
}

static void connection_close_handle(Connection* this, struct io_uring_cqe* cqe,
                                    struct io_uring* uring) {
    assert(this);
    assert(this->last_event.type == CLOSE);
    assert(cqe);
    assert(uring);

    if (cqe->res < 0)
        log_error(-cqe->res, "CLOSE");

    http_connection_free((HttpConnection*)this, uring);
}

static bool connection_recv_buf_init(Connection* this) {
    assert(this);

    return buf_init(&this->recv_buf, RECV_BUF_INITIAL_CAPACITY,
                    RECV_BUF_MAX_CAPACITY);
}

bool connection_send_buf_init(Connection* this) {
    assert(this);

    return buf_init(&this->send_buf, SEND_BUF_INITIAL_CAPACITY,
                    SEND_BUF_MAX_CAPACITY);
}

// Submit a request to receive as much data as the buffer can handle.
static bool connection_recv_submit(Connection* this, struct io_uring* uring,
                                   unsigned sqe_flags) {
    assert(this);
    assert(uring);
    (void)sqe_flags;

    if (!buf_initialized(&this->recv_buf) && !connection_recv_buf_init(this))
        return false;

    return event_recv_submit(&this->last_event, uring, this->socket,
                             buf_write_ptr(&this->recv_buf));
}

static bool connection_recv_handle(Connection* this, struct io_uring_cqe* cqe,
                                   struct io_uring* uring) {
    assert(this);
    assert(cqe);
    assert(uring);

    // In the event of an error, kill this connection.
    if (cqe->res < 0) {
        // If there was something wrong with the socket, pretend it was closed.
        if (cqe->res == -ENOTCONN || cqe->res == -EBADF ||
            cqe->res == -ENOTSOCK)
            this->last_event.type = CLOSE;
        return false;
    } else if (cqe->res == 0) {
        // EOF
        return connection_close_submit(this, uring);
    }

    // Update buffer pointers.
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

// Handle the completion of an ACCEPT event.
static bool connection_accept_handle(Connection* this, struct io_uring_cqe* cqe,
                                     struct io_uring* uring) {
    assert(this);
    assert(cqe);
    assert(uring);

    this->listener->accept_queued = false;

    if (cqe->res < 0)
        return false;

    log_msg(TRACE, "Accept connection.");

    this->socket = cqe->res;

    return this->recv_submit(this, uring, 0);
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
    case INVALID_EVENT:
        fprintf(stderr, "Got event with state INVALID.\n");
        return false;
    }

    // Unrecoverable connection error. Clean this one up.
    if (!rc) {
        ERR("Connection error. Dropping.");
        http_connection_free((HttpConnection*)this, uring);
    }

    return true;
}
