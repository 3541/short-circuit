#include "listen.h"

#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "config.h"
#include "connection.h"
#include "forward.h"

static fd socket_listen(in_port_t port) {
    fd ret;
    UNWRAPS(ret, socket(AF_INET, SOCK_STREAM, 0));

    const int enable = 1;
    UNWRAPSD(
        setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    UNWRAPSD(
        setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    UNWRAPSD(bind(ret, (struct sockaddr*)&addr, sizeof(addr)));
    UNWRAPSD(listen(ret, LISTEN_BACKLOG));

    return ret;
}

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
