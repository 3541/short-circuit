#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include "buffer.h"
#include "event.h"
#include "forward.h"
#include "http.h"
#include "socket.h"

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(Connection*, struct io_uring*, unsigned sqe_flags);
typedef bool (*ConnectionHandle)(Connection*, struct io_uring_cqe*,
                                 struct io_uring*);

typedef enum ConnectionTransport {
    PLAIN,
    TLS,
    NTRANSPORTS
} ConnectionTransport;

typedef struct Connection {
    // Has to be first so this can be cast to/from an Event.
    Event last_event;

    ConnectionTransport transport;

    fd                 socket;
    struct sockaddr_in client_addr;
    socklen_t          addr_len;

    ConnectionSubmit recv_submit;
    ConnectionHandle recv_handle;

    ConnectionSubmit send_submit;

    Buffer recv_buf;
    Buffer send_buf;

    HttpRequest request;

    // For the freelist.
    Connection* next;
} Connection;

void connection_reset(Connection*);

Connection* connection_accept_submit(struct io_uring*, ConnectionTransport,
                                     fd listen_socket);
bool        connection_send_submit(Connection*, struct io_uring*, unsigned sqe_flags);
bool        connection_close_submit(Connection*, struct io_uring*);

bool connection_send_buf_init(Connection*);

bool connection_event_dispatch(Connection*, struct io_uring_cqe*,
                               struct io_uring*, fd listen_socket);
