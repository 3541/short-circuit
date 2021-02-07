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

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(Connection*, struct io_uring*, uint32_t event_flags,
                                 uint8_t sqe_flags);
typedef bool (*ConnectionHandle)(Connection*, struct io_uring*, int32_t status, bool chain);

typedef enum ConnectionTransport { TRANSPORT_PLAIN, TRANSPORT_TLS } ConnectionTransport;

typedef struct Connection {
    EVENT_TARGET;

    A3Buffer recv_buf;
    A3Buffer send_buf;

    Timeout timeout;

    Listener* listener;

    ConnectionSubmit recv_submit;
    ConnectionHandle recv_handle;

    ConnectionSubmit send_submit;

    struct sockaddr_in client_addr;
    socklen_t          addr_len;
    fd                 socket;
    fd                 pipe[2];

    ConnectionTransport transport;
} Connection;

void connection_timeout_init(void);

bool connection_init(Connection*);
bool connection_reset(Connection*, struct io_uring*);

Connection* connection_accept_submit(Listener*, struct io_uring*);
bool connection_send_submit(Connection*, struct io_uring*, uint32_t send_flags, uint8_t sqe_flags);
bool connection_splice_submit(Connection*, struct io_uring*, fd src, size_t len, uint8_t sqe_flags);
bool connection_close_submit(Connection*, struct io_uring*);

void connection_event_handle(Connection*, struct io_uring*, EventType, int32_t status, bool chain);

A3_H_END
