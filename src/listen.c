#include "listen.h"

#include <assert.h>
#include <netinet/in.h>

void listener_init(Listener* this, in_port_t port,
                   ConnectionTransport transport) {
    assert(this);

    this->socket        = socket_listen(port);
    this->accept_queued = false;
    this->transport     = transport;
}

Connection* listener_accept_submit(Listener* this, struct io_uring* uring) {
    assert(this);
    assert(!this->accept_queued);
    assert(uring);

    Connection* ret = connection_accept_submit(this, uring);
    if (ret)
        this->accept_queued = true;

    return ret;
}

void listener_accept_all(Listener* listeners, size_t n_listeners,
                         struct io_uring* uring) {
    assert(listeners);
    assert(uring);

    for (size_t i = 0; i < n_listeners; i++)
        if (!listeners[i].accept_queued)
            listener_accept_submit(&listeners[i], uring);
}
