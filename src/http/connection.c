/*
 * SHORT CIRCUIT: HTTP CONNECTION -- HTTP-specific layer on top of a connection.
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

#include "http/connection.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/pool.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "file.h"
#include "http/types.h"
#include "uri.h"

static Pool* HTTP_CONNECTION_POOL = NULL;

void http_connection_pool_init() {
    HTTP_CONNECTION_POOL = POOL_OF(HttpConnection, CONNECTION_MAX_ALLOCATED);
}

HttpConnection* http_connection_new() {
    HttpConnection* ret = pool_alloc_block(HTTP_CONNECTION_POOL);

    if (ret && !http_connection_init(ret))
        http_connection_free(ret, NULL);

    return ret;
}

void http_connection_free(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    // If the socket hasn't been closed, arrange it. The close handle event will
    // call free when it's done.
    if (this->conn.socket != -1) {
        event_cancel_all(EVT(&this->conn), uring, IOSQE_IO_HARDLINK);
        // If the submission was successful, we're done for now.
        if (connection_close_submit(&this->conn, uring))
            return;

        // Make a last-ditch attempt to close, but do not block. Theoretically
        // this could cause a leak of sockets, but if both the close request and
        // the actual close here fail, there are probably larger issues at play.
        int flags = fcntl(this->conn.socket, F_GETFL);
        if (fcntl(this->conn.socket, F_SETFL, flags | O_NONBLOCK) != 0 ||
            close(this->conn.socket) != 0)
            log_error(errno, "Failed to close socket.");
    }

    http_connection_reset(this, uring);

    if (buf_initialized(&this->conn.recv_buf))
        buf_destroy(&this->conn.recv_buf);
    if (buf_initialized(&this->conn.send_buf))
        buf_destroy(&this->conn.send_buf);

    pool_free_block(HTTP_CONNECTION_POOL, this);
}

void http_connection_pool_free() { pool_free(HTTP_CONNECTION_POOL); }

bool http_connection_init(HttpConnection* this) {
    assert(this);

    TRYB(connection_init(&this->conn));

    this->state                       = CONNECTION_INIT;
    this->version                     = HTTP_VERSION_11;
    this->keep_alive                  = true;
    this->content_length              = -1;
    this->transfer_encodings          = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->target_file                 = NULL;
    this->response_transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;

    return true;
}

bool http_connection_reset(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (this->host.ptr)
        string_free(&this->host);

    if (this->target_path.ptr)
        string_free(&this->target_path);

    if (uri_is_initialized(&this->target))
        uri_free(&this->target);

    if (this->target_file) {
        file_close(this->target_file, uring);
        this->target_file = NULL;
    }

    return connection_reset(&this->conn, uring);
}
