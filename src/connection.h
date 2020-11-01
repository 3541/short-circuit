#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include "buffer.h"
#include "event.h"
#include "forward.h"
#include "http.h"
#include "types.h"

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(struct Connection*, struct io_uring*,
                                 int flags);
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

    ConnectionSubmit send_submit;

    struct Buffer recv_buf;
    struct Buffer send_buf;

    struct HttpRequest request;

    // For the freelist.
    struct Connection* next;
};

void connection_reset(struct Connection*);

struct Connection* connection_accept_submit(struct io_uring*,
                                            enum ConnectionTransport,
                                            fd listen_socket);
bool connection_send_submit(struct Connection*, struct io_uring*, int flags);
bool connection_close_submit(struct Connection*, struct io_uring*);

bool connection_send_buf_init(struct Connection*);

bool connection_event_dispatch(struct Connection*, struct io_uring_cqe*,
                               struct io_uring*, fd listen_socket);
