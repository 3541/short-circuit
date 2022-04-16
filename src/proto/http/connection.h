/*
 * SHORT CIRCUIT: HTTP CONNECTION -- HTTP-specific layer on top of a connection.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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

#include <sc/connection.h>

#include "request.h"
#include "response.h"

typedef struct ScHttpConnection {
    ScConnection* conn;

    ScHttpRequest  request;
    ScHttpResponse response;

    ScHttpVersion        version;
    ScHttpConnectionType connection_type;
} ScHttpConnection;

void sc_http_connection_handle(ScConnection*);
