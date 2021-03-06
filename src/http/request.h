/*
 * SHORT CIRCUIT: HTTP REQUEST -- HTTP request handlers.
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

#include <sys/types.h>

#include <a3/str.h>

#include "forward.h"
#include "http/headers.h"
#include "http/types.h"
#include "uri.h"

typedef struct HttpRequest {
    HttpHeaders headers;

    Uri                  target;
    A3CString            host;
    A3String             target_path;
    ssize_t              content_length;
    HttpTransferEncoding transfer_encodings;
} HttpRequest;

HttpConnection* http_request_connection(HttpRequest*);
HttpResponse*   http_request_response(HttpRequest*);

void http_request_init(HttpRequest*);
void http_request_reset(HttpRequest*);

bool http_request_handle(Connection*, struct io_uring*, bool success, int32_t status);
