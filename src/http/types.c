/*
 * SHORT CIRCUIT: HTTP TYPES -- Utility parsing and stringification for HTTP
 * types.
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

#include "http/types.h"

#include <assert.h>
#include <stddef.h>

#include <a3/str.h>
#include <a3/util.h>

HttpMethod http_request_method_parse(A3CString str) {
#define _METHOD(M, N) { M, A3_CS(N) },
    static const struct {
        HttpMethod method;
        A3CString  name;
    } HTTP_METHOD_NAMES[] = { HTTP_METHOD_ENUM };
#undef _METHOD

    if (!str.ptr || !*str.ptr)
        return HTTP_METHOD_INVALID;

    A3_TRYB_MAP(str.ptr && a3_string_isascii(str), HTTP_METHOD_INVALID);

    for (size_t i = 0; i < sizeof(HTTP_METHOD_NAMES) / sizeof(HTTP_METHOD_NAMES[0]); i++) {
        if (a3_string_cmpi(str, HTTP_METHOD_NAMES[i].name) == 0)
            return HTTP_METHOD_NAMES[i].method;
    }

    return HTTP_METHOD_UNKNOWN;
}

#define _VERSION(V, S) [V] = A3_CS(S),
static const A3CString HTTP_VERSION_STRINGS[] = { HTTP_VERSION_ENUM };
#undef _VERSION

A3CString http_version_string(HttpVersion version) { return HTTP_VERSION_STRINGS[version]; }

HttpVersion http_version_parse(A3CString str) {
    if (!str.ptr || !*str.ptr)
        return HTTP_VERSION_INVALID;

    A3_TRYB_MAP(str.ptr && a3_string_isascii(str), HTTP_VERSION_INVALID);

    for (HttpVersion v = HTTP_VERSION_INVALID + 1; v < HTTP_VERSION_UNKNOWN; v++)
        if (a3_string_cmpi(str, HTTP_VERSION_STRINGS[v]) == 0)
            return v;

    return HTTP_VERSION_UNKNOWN;
}

#define _STATUS(CODE, TYPE, REASON) [TYPE] = { CODE, A3_CS(REASON) },
static const struct {
    uint16_t  code;
    A3CString reason;
} HTTP_STATUSES[] = { HTTP_STATUS_ENUM };
#undef STATUS

A3CString http_status_reason(HttpStatus status) { return HTTP_STATUSES[status].reason; }

uint16_t http_status_code(HttpStatus status) { return HTTP_STATUSES[status].code; }

A3CString http_content_type_name(HttpContentType type) {
#define _CTYPE(T, S) [T]                             = A3_CS(S),
    static const A3CString HTTP_CONTENT_TYPE_NAMES[] = { HTTP_CONTENT_TYPE_ENUM };
#undef _CTYPE

    return HTTP_CONTENT_TYPE_NAMES[type];
}

HttpContentType http_content_type_from_path(A3CString path) {
    assert(path.ptr);

    static struct {
        A3CString       ext;
        HttpContentType ctype;
    } EXTENSIONS[] = {
        { A3_CS("bmp"), HTTP_CONTENT_TYPE_IMAGE_BMP },
        { A3_CS("gif"), HTTP_CONTENT_TYPE_IMAGE_GIF },
        { A3_CS("ico"), HTTP_CONTENT_TYPE_IMAGE_ICO },
        { A3_CS("jpg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { A3_CS("jpeg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { A3_CS("json"), HTTP_CONTENT_TYPE_APPLICATION_JSON },
        { A3_CS("pdf"), HTTP_CONTENT_TYPE_APPLICATION_PDF },
        { A3_CS("png"), HTTP_CONTENT_TYPE_IMAGE_PNG },
        { A3_CS("svg"), HTTP_CONTENT_TYPE_IMAGE_SVG },
        { A3_CS("webp"), HTTP_CONTENT_TYPE_IMAGE_WEBP },
        { A3_CS("css"), HTTP_CONTENT_TYPE_TEXT_CSS },
        { A3_CS("js"), HTTP_CONTENT_TYPE_TEXT_JAVASCRIPT },
        { A3_CS("md"), HTTP_CONTENT_TYPE_TEXT_MARKDOWN },
        { A3_CS("txt"), HTTP_CONTENT_TYPE_TEXT_PLAIN },
        { A3_CS("htm"), HTTP_CONTENT_TYPE_TEXT_HTML },
        { A3_CS("html"), HTTP_CONTENT_TYPE_TEXT_HTML },
    };

    A3CString last_dot = a3_string_rchr(path, '.');
    if (!last_dot.ptr || last_dot.len < 2)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    A3CString last_slash = a3_string_rchr(path, '/');
    if (last_slash.ptr && last_slash.ptr > last_dot.ptr)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    A3CString ext = { .ptr = last_dot.ptr + 1, .len = last_dot.len - 1 };
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        if (a3_string_cmpi(ext, EXTENSIONS[i].ext) == 0)
            return EXTENSIONS[i].ctype;
    }

    return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;
}

HttpTransferEncoding http_transfer_encoding_parse(A3CString value) {
#define _TENCODING(E, S) { HTTP_##E, A3_CS(S) },
    static const struct {
        HttpTransferEncoding encoding;
        A3CString            value;
    } HTTP_TRANSFER_ENCODING_VALUES[] = { HTTP_TRANSFER_ENCODING_ENUM };
#undef _TENCODING

    assert(value.ptr && *value.ptr);

    A3_TRYB_MAP(value.ptr && a3_string_isascii(value), HTTP_TRANSFER_ENCODING_INVALID);

    for (size_t i = 0;
         i < sizeof(HTTP_TRANSFER_ENCODING_VALUES) / sizeof(HTTP_TRANSFER_ENCODING_VALUES[0]);
         i++) {
        if (a3_string_cmpi(value, HTTP_TRANSFER_ENCODING_VALUES[i].value) == 0)
            return HTTP_TRANSFER_ENCODING_VALUES[i].encoding;
    }

    return HTTP_TRANSFER_ENCODING_INVALID;
}
