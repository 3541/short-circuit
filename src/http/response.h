/*
 * SHORT CIRCUIT: HTTP RESPONSE -- HTTP response submission.
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

#include <stdbool.h>

#include "forward.h"
#include "http/types.h"

typedef struct HttpResponse {
    HttpContentType      content_type;
    HttpTransferEncoding transfer_encodings;
} HttpResponse;

void http_response_init(HttpResponse*);
void http_response_reset(HttpResponse*);

bool http_response_handle(HttpConnection*, struct io_uring*);
bool http_response_splice_handle(HttpConnection*, struct io_uring*, bool success, int32_t status);

#define HTTP_RESPONSE_CLOSE true
#define HTTP_RESPONSE_ALLOW false
bool http_response_error_submit(HttpResponse*, struct io_uring*, HttpStatus, bool close);

bool http_response_file_submit(HttpResponse*, struct io_uring*);
