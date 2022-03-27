/*
 * SHORT CIRCUIT: HTTP REQUEST -- HTTP request handling.
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

#include <assert.h>

#include <a3/log.h>

#include <sc/connection.h>

#include "connection.h"
#include "request.h"

void sc_http_request_handle(ScConnection* conn) {
    assert(conn);

    A3_TRACE("Handling HTTP connection.");
    ScHttpConnection http = { .conn = conn, .request = { { 0 } } };
    (void)http;
}
