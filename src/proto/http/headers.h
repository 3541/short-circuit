/*
 * SHORT CIRCUIT: HTTP HEADERS -- HTTP header parsing and storage.
 *
 * Copyright (c) 2021-2022, Alex O'Brien <3541ax@gmail.com>
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

#include <stdint.h>

#include <a3/ht.h>
#include <a3/str.h>

#include <sc/http.h>

A3_HT_DEFINE_STRUCTS(A3CString, A3String);
A3_HT_DECLARE_METHODS(A3CString, A3String);

typedef A3_HT(A3CString, A3String) ScHttpHeaders;

void sc_http_headers_init(ScHttpHeaders*);
void sc_http_headers_destroy(ScHttpHeaders*);

bool     sc_http_header_add(ScHttpHeaders*, A3CString name, A3CString value);
bool     sc_http_header_set(ScHttpHeaders*, A3CString name, A3CString value);
bool     sc_http_header_set_fmt(ScHttpHeaders*, A3CString name, char const* fmt, ...);
bool     sc_http_header_set_num(ScHttpHeaders*, A3CString name, uint64_t);
A3String sc_http_header_get(ScHttpHeaders*, A3CString name);

ScHttpConnectionType   sc_http_header_connection(ScHttpHeaders*);
ScHttpTransferEncoding sc_http_header_transfer_encoding(ScHttpHeaders*);
ssize_t                sc_http_header_content_length(ScHttpHeaders*);

size_t sc_http_headers_count(ScHttpHeaders*);

#define SC_HTTP_HEADERS_FOR_EACH(H, N, V) A3_HT_FOR_EACH(A3CString, A3String, (H), N, V)
#define SC_HTTP_HEADER_FOR_EACH_VALUE(HEADERS, NAME, VAL)                                          \
    A3String _header_val = sc_http_header_get((HEADERS), (NAME));                                  \
    A3Buffer _header_buf = {                                                                       \
        .data = _header_val, .tail = _header_val.len, .head = 0, .max_cap = _header_val.len        \
    };                                                                                             \
    for (A3CString VAL = A3_S_CONST(a3_buf_token_next(&_header_buf, A3_CS(","), A3_PRES_END_NO));  \
         VAL.ptr && a3_buf_len(&_header_buf);                                                      \
         VAL = A3_S_CONST(a3_buf_token_next(&_header_buf, A3_CS(","), A3_PRES_END_NO)))
