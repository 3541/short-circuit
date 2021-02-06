/*
 * SHORT CIRCUIT: HTTP HEADERS -- HTTP header parsing and storage.
 *
 * Copyright (c) 2021, Alex O'Brien <3541ax@gmail.com>
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

#include <a3/buffer.h>
#include <a3/ht.h>
#include <a3/str.h>

#include "forward.h"
#include "http/types.h"

A3_HT_DEFINE_STRUCTS(A3CString, A3String);

typedef struct HttpHeaders {
    A3_HT(A3CString, A3String) headers;
} HttpHeaders;

void http_headers_init(HttpHeaders*);
void http_headers_destroy(HttpHeaders*);

bool      http_header_add(HttpHeaders*, A3CString name, A3CString value);
A3String http_header_get(HttpHeaders*, A3CString name);

HttpConnectionType   http_header_connection(HttpHeaders*);
HttpTransferEncoding http_header_transfer_encodings(HttpHeaders*);
ssize_t              http_header_content_length(HttpHeaders*);

#define HTTP_HEADER_FOR_EACH_VALUE(HEADERS, NAME, VAL)                                             \
    A3String _header_val = http_header_get((HEADERS), (NAME));                                     \
    A3Buffer _header_buf = {                                                                       \
        .data = _header_val, .tail = _header_val.len, .head = 0, .max_cap = _header_val.len        \
    };                                                                                             \
    for (A3CString VAL = A3_S_CONST(a3_buf_token_next(&_header_buf, A3_CS(",")));                  \
         VAL.ptr && a3_buf_len(&_header_buf);                                                      \
         VAL = A3_S_CONST(a3_buf_token_next(&_header_buf, A3_CS(","))))
