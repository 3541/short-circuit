#pragma once

#include <netinet/in.h>
#include <stdbool.h>
#include <sys/socket.h>

#include <a3/buffer.h>

#include "event.h"
#include "forward.h"
#include "socket.h"
#include "timeout.h"

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(Connection*, struct io_uring*,
                                 uint8_t sqe_flags);
typedef bool (*ConnectionHandle)(Connection*, struct io_uring*,
                                 struct io_uring_cqe*);

typedef enum ConnectionTransport {
    PLAIN,
    TLS,
    NTRANSPORTS
} ConnectionTransport;

typedef struct Connection {
    // Has to be first so this can be cast to/from an Event.
    Event last_event;

    Listener* listener;

    ConnectionTransport transport;

    fd                 socket;
    struct sockaddr_in client_addr;
    socklen_t          addr_len;

    ConnectionSubmit recv_submit;
    ConnectionHandle recv_handle;

    ConnectionSubmit send_submit;

    Buffer recv_buf;
    Buffer send_buf;

    Timeout timeout;

    // For the freelist.
    Connection* next;
} Connection;

void connection_timeout_init(void);

bool connection_init(Connection*);
bool connection_reset(Connection*, struct io_uring*);

Connection* connection_accept_submit(Listener*, struct io_uring*);
bool connection_send_submit(Connection*, struct io_uring*, uint8_t sqe_flags);
bool connection_close_submit(Connection*, struct io_uring*);

bool connection_event_dispatch(Connection*, struct io_uring*,
                               struct io_uring_cqe*);
