/*
 * SHORT CIRCUIT: HTTP RESPONSE -- HTTP response submission.
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

#include <sc/http.h>
#include <sc/mime.h>

#include "headers.h"

typedef struct ScHttpResponse {
    ScHttpHeaders headers;
    ScMimeType    content_type;
} ScHttpResponse;

void sc_http_response_init(ScHttpResponse*);
void sc_http_response_reset(ScHttpResponse*);
void sc_http_response_destroy(ScHttpResponse*);

void sc_http_response_send(ScHttpResponse*, ScHttpStatus);
void sc_http_response_file_send(ScHttpResponse*, ScFd file);

#define SC_HTTP_CLOSE true
#define SC_HTTP_KEEP  false
void sc_http_response_error_send(ScHttpResponse*, ScHttpStatus, bool close);
