#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include "buffer.h"
#include "event.h"
#include "forward.h"
#include "types.h"

struct Connection {
    // Has to be first so this can be cast to an Event.
    struct Event       last_event;
    fd                 socket;
    struct sockaddr_in client_addr;
    socklen_t          addr_len;

    struct Buffer recv_buf;

    // For the freelist.
    struct Connection* next;
};

struct Connection* connection_accept_submit(struct io_uring*, fd listen_socket);
bool connection_event_dispatch(struct Connection*, struct io_uring_cqe*,
                               struct io_uring*, fd listen_socket);
