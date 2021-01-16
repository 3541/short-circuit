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

#pragma once

#include <liburing.h>
#include <stdbool.h>

#include <a3/str.h>

#include "../connection.h"
#include "file.h"
#include "http/request.h"
#include "http/response.h"
#include "http/types.h"

#define HTTP_NEWLINE CS("\r\n")

typedef enum HttpConnectionState {
    CONNECTION_INIT,
    CONNECTION_PARSED_FIRST_LINE,
    CONNECTION_PARSED_HEADERS,
    CONNECTION_OPENING_FILE,
    CONNECTION_RESPONDING,
    CONNECTION_CLOSING,
} HttpConnectionState;

typedef struct HttpConnection {
    Connection conn;

    HttpConnectionState state;
    HttpVersion         version;
    HttpMethod          method;
    bool                keep_alive;

    HttpRequest  request;
    HttpResponse response;

    FileHandle* target_file;
} HttpConnection;

void            http_connection_pool_init(void);
HttpConnection* http_connection_new(void);
void            http_connection_free(HttpConnection*, struct io_uring*);
void            http_connection_pool_free(void);

bool http_connection_init(HttpConnection*);
bool http_connection_close_submit(HttpConnection*, struct io_uring*);
bool http_connection_reset(HttpConnection*, struct io_uring*);
