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

#pragma once

#include <sc/forward.h>
#include <sc/http.h>
#include <sc/uri.h>

#include "headers.h"

typedef struct ScHttpRequest {
    ScHttpHeaders          headers;
    ScUri                  target;
    A3CString              host;
    ScHttpMethod           method;
    ScHttpTransferEncoding transfer_encodings;
    ssize_t                content_length;
} ScHttpRequest;

void sc_http_request_init(ScHttpRequest*);
void sc_http_request_reset(ScHttpRequest*);
void sc_http_request_destroy(ScHttpRequest*);
void sc_http_request_handle(ScHttpRequest*);

ScHttpConnection* sc_http_request_connection(ScHttpRequest*);
ScHttpResponse*   sc_http_request_response(ScHttpRequest*);
