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
#include "http.h"
#include "log.h"
#include "types.h"
#include "util.h"

static struct Connection* connection_freelist                   = NULL;
static size_t             connections_allocated                 = 0;
static bool               connection_accept_queued[NTRANSPORTS] = { false };

static bool connection_close_submit(struct Connection*, struct io_uring*);

static struct Connection* connection_new() {
    struct Connection* ret = NULL;
    if (connection_freelist) {
        ret                 = connection_freelist;
        connection_freelist = ret->next;
        memset(ret, 0, sizeof(struct Connection));
    } else if (connections_allocated < MAX_ALLOCATED_CONNECTIONS) {
        ret = calloc(1, sizeof(struct Connection));
    } else {
        ERR("Too many connections allocated.");
    }

    return ret;
}

static void connection_free(struct Connection* this, struct io_uring* uring) {
    assert(this);

    // If the socket hasn't been closed, arrange it. The close handle event will
    // call free when it's done.
    if (this->last_event.type != INVALID && this->last_event.type != CLOSE) {
        // If the submission was successful, we're done.
        if (connection_close_submit(this, uring))
            return;

        // Make a last-ditch attempt to close, but do not block. Theoretically
        // this could cause a leak of sockets, but if both the close request and
        // the actual close here fail, there are probably larger issues at play.
        int flags = fcntl(this->socket, F_GETFL);
        if (fcntl(this->socket, F_SETFL, flags | O_NONBLOCK) == 0)
            close(this->socket);
    }

    buf_free(&this->recv_buf);

    this->next          = connection_freelist;
    connection_freelist = this;
}

static bool connection_close_submit(struct Connection* this,
                                    struct io_uring* uring) {
    assert(this);
    assert(uring);

    return event_close_submit(&this->last_event, uring, this->socket);
}

static void connection_close_handle(struct Connection* this,
                                    struct io_uring_cqe* cqe,
                                    struct io_uring*     uring) {
    assert(this);
    assert(this->last_event.type == CLOSE);
    assert(cqe);
    assert(uring);

    connection_free(this, uring);
}

static bool connection_recv_buf_init(struct Connection* this) {
    assert(this);

    return buf_init(&this->recv_buf, RECV_BUF_INITIAL_CAPACITY);
}

// Submit a request to receive as much data as the buffer can handle.
static bool connection_recv_submit(struct Connection* this,
                                   struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (!buf_initialized(&this->recv_buf) && !connection_recv_buf_init(this))
        return false;

    return event_recv_submit(&this->last_event, uring, this->socket,
                             buf_write_ptr(&this->recv_buf),
                             buf_cap(&this->recv_buf));
}

static bool connection_recv_handle(struct Connection* this,
                                   struct io_uring_cqe* cqe,
                                   struct io_uring*     uring) {
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
        PANIC("TODO: EOF");
    }

    // Update buffer pointers.
    buf_wrote(&this->recv_buf, cqe->res);

    int8_t rc = http_request_parse(this, uring);
    if (rc < 0) {
        return false;
    } else if (rc == 0) {
        // Need more data.
        return this->recv_submit(this, uring);
    }

    return true;
}

// Submit an ACCEPT on the uring.
struct Connection* connection_accept_submit(struct io_uring*         uring,
                                            enum ConnectionTransport transport,
                                            fd listen_socket) {
    assert(uring);

    struct Connection* ret = connection_new();
    if (!ret)
        return NULL;

    ret->transport = transport;
    switch (transport) {
    case PLAIN:
        ret->recv_submit = connection_recv_submit;
        ret->recv_handle = connection_recv_handle;
        break;
    case TLS:
        PANIC("TODO: TLS");
    default:
        PANIC("Invalid transport.");
    }

    ret->addr_len = sizeof(ret->client_addr);

    if (!event_accept_submit(&ret->last_event, uring, listen_socket,
                             &ret->client_addr, &ret->addr_len)) {
        free(ret);
        return NULL;
    }

    connection_accept_queued[transport] = true;
    return ret;
}

// Handle the completion of an ACCEPT event.
static bool connection_accept_handle(struct Connection* this,
                                     struct io_uring_cqe* cqe,
                                     struct io_uring* uring, fd listen_socket) {
    assert(this);
    assert(cqe);
    assert(uring);

    if (cqe->res < 0)
        return false;

    log_msg(TRACE, "Accept connection.");

    connection_accept_queued[this->transport] = false;

    // First, renew the accept request. Failing to renew is not a fatal error,
    // and every event will try again to enqueue a new accept request if this
    // happens.
    if (connection_accept_submit(uring, this->transport, listen_socket))
        connection_accept_queued[this->transport] = true;

    this->socket = cqe->res;

    return this->recv_submit(this, uring);
}

// Dispatch an event pertaining to a connection. Returns false to die.
bool connection_event_dispatch(struct Connection* this,
                               struct io_uring_cqe* cqe, struct io_uring* uring,
                               fd listen_socket) {
    assert(this);
    assert(cqe);
    assert(uring);

    bool rc = true;

    switch (this->last_event.type) {
    case ACCEPT:
        rc = connection_accept_handle(this, cqe, uring, listen_socket);
        break;
    case RECV:
        rc = this->recv_handle(this, cqe, uring);
        break;
    case CLOSE:
        connection_close_handle(this, cqe, uring);
        break;
    case INVALID:
        fprintf(stderr, "Got event with state INVALID.\n");
        return false;
    }

    // If there isn't an accept request in flight, try to make a new one.
    if (!connection_accept_queued[this->transport])
        connection_accept_submit(uring, this->transport, listen_socket);

    // Unrecoverable connection error. Clean this one up.
    if (!rc) {
        ERR("Connection error. Dropping.");
        connection_free(this, uring);
    }

    return true;
}
