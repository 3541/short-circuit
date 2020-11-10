#pragma once

#include <stdbool.h>

#include "connection.h"
#include "socket.h"

typedef struct Listener {
    fd                  socket;
    ConnectionTransport transport;
    bool                accept_queued;
} Listener;

void        listener_init(Listener*, in_port_t, ConnectionTransport);
Connection* listener_accept_submit(Listener*, struct io_uring*);
void listener_accept_all(Listener*, size_t n_listeners, struct io_uring*);
