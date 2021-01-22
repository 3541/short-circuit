/*
 * SHORT CIRCUIT: LISTEN -- Socket listener. Keeps an accept event queued on a
 * given socket.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "listen.h"

#include <assert.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>

#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "forward.h"

static fd socket_listen(in_port_t port) {
    fd ret;
    A3_UNWRAPS(ret, socket(AF_INET, SOCK_STREAM, 0));

    const int enable = 1;
    A3_UNWRAPSD(
        setsockopt(ret, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)));
    A3_UNWRAPSD(
        setsockopt(ret, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    A3_UNWRAPSD(bind(ret, (struct sockaddr*)&addr, sizeof(addr)));
    A3_UNWRAPSD(listen(ret, LISTEN_BACKLOG));

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
