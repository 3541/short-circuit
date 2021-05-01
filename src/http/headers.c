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

#include "http/headers.h"
#include "http/types.h"

#include <a3/ht.h>
#include <a3/str.h>

A3_HT_DECLARE_METHODS(A3CString, A3String);
A3_HT_DEFINE_METHODS(A3CString, A3String, A3_CS_PTR, A3_S_LEN, a3_string_cmp);

static bool http_headers_combine(A3String* current_value, A3String new_value) {
    assert(current_value);

    A3String combined_value = a3_string_alloc(current_value->len + new_value.len + 1);
    a3_string_concat(combined_value, 3, *current_value, A3_CS(","), new_value);

    a3_string_free(current_value);
    a3_string_free(&new_value);
    *current_value = combined_value;

    return true;
}

void http_headers_init(HttpHeaders* headers) {
    assert(headers);

    A3_HT_INIT(A3CString, A3String)(&headers->headers, A3_HT_ALLOW_GROWTH);
    A3_HT_SET_DUPLICATE_CB(A3CString, A3String)(&headers->headers, http_headers_combine);
}

void http_headers_destroy(HttpHeaders* headers) {
    assert(headers);

    A3_HT_FOR_EACH(A3CString, A3String, &headers->headers, key, value) {
        a3_string_free((A3String*)key);
        a3_string_free((A3String*)value);
    }

    A3_HT_DESTROY(A3CString, A3String)(&headers->headers);
}

bool http_header_add(HttpHeaders* headers, A3CString name, A3CString value) {
    assert(headers);
    assert(name.ptr);
    assert(value.ptr);

    A3String key = a3_string_to_lowercase(name);

    return A3_HT_INSERT(A3CString, A3String)(&headers->headers, A3_S_CONST(key),
                                             a3_string_clone(value));
}

A3String http_header_get(HttpHeaders* headers, A3CString name) {
    assert(headers);
    assert(name.ptr);

    A3String key = a3_string_to_lowercase(name);

    A3String* ret = A3_HT_FIND(A3CString, A3String)(&headers->headers, A3_S_CONST(key));
    a3_string_free(&key);
    if (!ret)
        return A3_S_NULL;
    return *ret;
}

HttpConnectionType http_header_connection(HttpHeaders* headers) {
    assert(headers);

    A3String* connection = A3_HT_FIND(A3CString, A3String)(&headers->headers, A3_CS("connection"));
    if (!connection)
        return HTTP_CONNECTION_TYPE_UNSPECIFIED;

    A3CString conn = A3_S_CONST(*connection);
    if (a3_string_cmpi(conn, A3_CS("Keep-Alive")) == 0)
        return HTTP_CONNECTION_TYPE_KEEP_ALIVE;
    else if (a3_string_cmpi(conn, A3_CS("Close")) == 0)
        return HTTP_CONNECTION_TYPE_CLOSE;
    else
        return HTTP_CONNECTION_TYPE_INVALID;
}

HttpTransferEncoding http_header_transfer_encodings(HttpHeaders* headers) {
    assert(headers);

    HttpTransferEncoding ret = HTTP_TRANSFER_ENCODING_INVALID;

    HTTP_HEADER_FOR_EACH_VALUE(headers, A3_CS("Transfer-Encoding"), encoding) {
        HttpTransferEncoding new_encoding = http_transfer_encoding_parse(encoding);
        if (!new_encoding)
            return HTTP_TRANSFER_ENCODING_INVALID;
        ret |= new_encoding;
    }

    if (!ret)
        return HTTP_TRANSFER_ENCODING_IDENTITY;
    return ret;
}

ssize_t http_header_content_length(HttpHeaders* headers) {
    assert(headers);

    ssize_t ret = HTTP_CONTENT_LENGTH_UNSPECIFIED;

    HTTP_HEADER_FOR_EACH_VALUE(headers, A3_CS("Content-Length"), content_length) {
        char*   endptr     = NULL;
        ssize_t new_length = strtol(A3_S_AS_C_STR(content_length), &endptr, 10);

        if (*endptr != '\0' || (ret != HTTP_CONTENT_LENGTH_UNSPECIFIED && ret != new_length) ||
            new_length < 0)
            return HTTP_CONTENT_LENGTH_INVALID;

        ret = new_length;
    }

    return ret;
}
