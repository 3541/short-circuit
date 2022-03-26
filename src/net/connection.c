/*
 * SHORT CIRCUIT: CONNECTION -- Abstract connection.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#include "connection.h"

#include <a3/buffer.h>
#include <a3/log.h>

#include <sc/coroutine.h>

#include "config.h"
#include "listen.h"

ssize_t sc_connection_handle(ScCoroutine* self, void* data) {
    assert(self);
    assert(data);

    A3_TRACE("Handling connection.");
    ScConnection* conn = data;
    return conn->listener->connection_handler(conn);
}

ScConnection* sc_connection_new(ScListener* listener) {
    assert(listener);

    A3_UNWRAPNI(ScConnection*, ret, calloc(1, sizeof(*ret)));

    a3_buf_init(&ret->send_buf, SC_SEND_BUF_INIT_CAP, SC_SEND_BUF_MAX_CAP);
    a3_buf_init(&ret->recv_buf, SC_RECV_BUF_INIT_CAP, SC_RECV_BUF_MAX_CAP);

    ret->addr_len  = sizeof(ret->client_addr);
    ret->coroutine = NULL;
    ret->listener  = listener;
    ret->socket    = -1;

    return ret;
}
