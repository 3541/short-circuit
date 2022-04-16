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

#include "headers.h"

#include <assert.h>

#include <a3/buffer.h>
#include <a3/ht.h>
#include <a3/str.h>

#include "sc/http.h"

A3_HT_DEFINE_METHODS(A3CString, A3String, a3_string_cptr, a3_string_len, a3_string_cmp);

static bool sc_http_headers_combine(A3String* current_value, A3String new_value) {
    assert(current_value);

    A3String combined_value = a3_string_alloc(current_value->len + new_value.len + 1);
    a3_string_concat(combined_value, 3, *current_value, A3_CS(","), new_value);

    a3_string_free(current_value);
    a3_string_free(&new_value);
    *current_value = combined_value;

    return true;
}

void sc_http_headers_init(ScHttpHeaders* headers) {
    assert(headers);

    A3_HT_INIT(A3CString, A3String)(headers, A3_HT_NO_HASH_KEY, A3_HT_ALLOW_GROWTH);
    A3_HT_SET_DUPLICATE_CB(A3CString, A3String)(headers, sc_http_headers_combine);
}

void sc_http_headers_destroy(ScHttpHeaders* headers) {
    assert(headers);

    A3_HT_FOR_EACH(A3CString, A3String, headers, key, value) {
        a3_string_free((A3String*)key);
        a3_string_free(value);
    }

    A3_HT_DESTROY(A3CString, A3String)(headers);
}

bool sc_http_header_add(ScHttpHeaders* headers, A3CString name, A3CString value) {
    assert(headers);
    assert(name.ptr);
    assert(value.ptr);

    A3String key = a3_string_to_lowercase(name);
    return A3_HT_INSERT(A3CString, A3String)(headers, A3_S_CONST(key), a3_string_clone(value));
}

bool sc_http_header_set(ScHttpHeaders* headers, A3CString name, A3CString value) {
    assert(headers);
    assert(name.ptr);
    assert(value.ptr);

    A3String  key  = a3_string_to_lowercase(name);
    A3String* prev = A3_HT_FIND(A3CString, A3String)(headers, A3_S_CONST(key));
    if (prev) {
        a3_string_free(prev);
        A3_HT_DELETE(A3CString, A3String)(headers, A3_S_CONST(key));
    }

    return A3_HT_INSERT(A3CString, A3String)(headers, A3_S_CONST(key), a3_string_clone(value));
}

bool sc_http_header_set_num(ScHttpHeaders* headers, A3CString name, uint64_t n) {
    assert(headers);
    assert(name.ptr);

    uint8_t buf[20] = { '\0' };
    return sc_http_header_set(
        headers, name,
        A3_S_CONST(a3_string_itoa_into((A3String) { .ptr = buf, .len = sizeof(buf) }, n)));
}

A3String sc_http_header_get(ScHttpHeaders* headers, A3CString name) {
    assert(headers);
    assert(name.ptr);

    A3String  key = a3_string_to_lowercase(name);
    A3String* ret = A3_HT_FIND(A3CString, A3String)(headers, A3_S_CONST(key));
    a3_string_free(&key);

    return ret ? *ret : A3_S_NULL;
}

ScHttpConnectionType sc_http_header_connection(ScHttpHeaders* headers) {
    assert(headers);

    A3String* connection = A3_HT_FIND(A3CString, A3String)(headers, A3_CS("connection"));
    if (!connection)
        return SC_HTTP_CONNECTION_TYPE_UNSPECIFIED;

#define CTYPE(TY, S) [TY]                             = A3_CS(S),
    static A3CString const SC_HTTP_CONNECTION_TYPES[] = { SC_HTTP_CONNECTION_TYPE_ENUM };
#undef CTYPE

    for (ScHttpConnectionType i = 0;
         i < sizeof(SC_HTTP_CONNECTION_TYPES) / sizeof(SC_HTTP_CONNECTION_TYPES[0]); i++) {
        if (a3_string_cmpi(*connection, SC_HTTP_CONNECTION_TYPES[i]) == 0)
            return i;
    }

    return SC_HTTP_CONNECTION_TYPE_INVALID;
}

static ScHttpTransferEncoding sc_http_transfer_encoding_parse(A3CString encoding) {
    assert(encoding.ptr && *encoding.ptr);

#define ENCODING(TY, S) [TY##_BITS]                = A3_CS(S),
    static A3CString const SC_TRANSFER_ENCODINGS[] = { SC_HTTP_TRANSFER_ENCODING_ENUM };
#undef ENCODINGS

    for (ScHttpTransferBits i = 0;
         i < sizeof(SC_TRANSFER_ENCODINGS) / sizeof(SC_TRANSFER_ENCODINGS[0]); i++) {
        if (a3_string_cmpi(encoding, SC_TRANSFER_ENCODINGS[i]) == 0)
            return 1 << i;
    }

    return SC_HTTP_TRANSFER_ENCODING_INVALID;
}

ScHttpTransferEncoding sc_http_header_transfer_encoding(ScHttpHeaders* headers) {
    assert(headers);

    ScHttpTransferEncoding ret = SC_HTTP_TRANSFER_ENCODING_IDENTITY;

    SC_HTTP_HEADER_FOR_EACH_VALUE(headers, A3_CS("Transfer-Encoding"), encoding) {
        ScHttpTransferEncoding new_encoding = sc_http_transfer_encoding_parse(encoding);
        if (!new_encoding)
            return SC_HTTP_TRANSFER_ENCODING_INVALID;
        ret |= new_encoding;
    }

    return ret;
}

ssize_t sc_http_header_content_length(ScHttpHeaders* headers) {
    assert(headers);

    ssize_t ret = SC_HTTP_CONTENT_LENGTH_UNSPECIFIED;

    SC_HTTP_HEADER_FOR_EACH_VALUE(headers, A3_CS("Content-Length"), content_length) {
        char*   endptr     = NULL;
        ssize_t new_length = strtol(a3_string_cstr(content_length), &endptr, 10);

        if (*endptr != '\0' || (ret != SC_HTTP_CONTENT_LENGTH_UNSPECIFIED && ret != new_length) ||
            new_length < 0)
            return SC_HTTP_CONTENT_LENGTH_INVALID;

        ret = new_length;
    }

    return ret;
}

size_t sc_http_headers_count(ScHttpHeaders* headers) {
    assert(headers);

    return A3_HT_SIZE(A3CString, A3String)(headers);
}
