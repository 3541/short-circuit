#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include "buffer.h"
#include "event.h"
#include "forward.h"
#include "types.h"

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(struct Connection*, struct io_uring*);
typedef bool (*ConnectionHandle)(struct Connection*, struct io_uring_cqe*,
                                 struct io_uring*);

enum ConnectionTransport { PLAIN, TLS, NTRANSPORTS };

struct Connection {
    // Has to be first so this can be cast to/from an Event.
    struct Event last_event;

    enum ConnectionTransport transport;

    fd                 socket;
    struct sockaddr_in client_addr;
    socklen_t          addr_len;

    ConnectionSubmit recv_submit;
    ConnectionHandle recv_handle;

    struct Buffer recv_buf;

    // For the freelist.
    struct Connection* next;
};

struct Connection* connection_accept_submit(struct io_uring*,
                                            enum ConnectionTransport,
                                            fd listen_socket);
bool connection_event_dispatch(struct Connection*, struct io_uring_cqe*,
                               struct io_uring*, fd listen_socket);
