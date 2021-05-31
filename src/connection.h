/*
 * SHORT CIRCUIT: CONNECTION -- Abstract connection on top of the event
 * interface.
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

#pragma once

#include <liburing.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#include <a3/buffer.h>
#include <a3/cpp.h>

#include "event.h"
#include "forward.h"
#include "timeout.h"

A3_H_BEGIN

typedef enum { SPLICE_IN, SPLICE_OUT } SpliceDirection;

typedef bool (*ConnectionHandler)(Connection*, struct io_uring*, bool success, int32_t status);
typedef bool (*ConnectionSpliceHandler)(Connection*, struct io_uring*, SpliceDirection,
                                        bool success, int32_t status);

typedef enum ConnectionTransport { TRANSPORT_PLAIN, TRANSPORT_TLS } ConnectionTransport;

typedef struct Connection {
    EVENT_TARGET;

    A3Buffer recv_buf;
    A3Buffer send_buf;

    Timeout timeout;

    Listener* listener;

    struct sockaddr_in client_addr;
    socklen_t          addr_len;
    fd                 socket;
    fd                 pipe[2];

    ConnectionTransport transport;
} Connection;

void connection_timeout_init(void);

bool connection_init(Connection*);
bool connection_reset(Connection*, struct io_uring*);

Connection* connection_accept_submit(Listener*, struct io_uring*, ConnectionHandler);
bool        connection_recv_submit(Connection*, struct io_uring*, ConnectionHandler);
bool connection_send_submit(Connection*, struct io_uring*, ConnectionHandler, uint32_t send_flags,
                            uint8_t sqe_flags);
bool connection_splice_submit(Connection*, struct io_uring*, ConnectionSpliceHandler,
                              ConnectionHandler, fd src, size_t file_offset, size_t len,
                              uint8_t sqe_flags);
bool connection_splice_retry(Connection*, struct io_uring*, ConnectionSpliceHandler,
                             ConnectionHandler, fd src, size_t in_buf, size_t file_offset,
                             size_t remaining, uint8_t sqe_flags);
bool connection_close_submit(Connection*, struct io_uring*, ConnectionHandler);

A3_H_END
