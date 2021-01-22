/*
 * SHORT CIRCUIT: HTTP TYPES -- Fundamental types for HTTP handling and parsing.
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

#include <stdint.h>

#include <a3/str.h>

#define HTTP_NEWLINE A3_CS("\r\n")

#define HTTP_METHOD_ENUM                                                       \
    _METHOD(HTTP_METHOD_INVALID, "__INVALID")                                  \
    _METHOD(HTTP_METHOD_BREW, "BREW")                                          \
    _METHOD(HTTP_METHOD_GET, "GET")                                            \
    _METHOD(HTTP_METHOD_HEAD, "HEAD")                                          \
    _METHOD(HTTP_METHOD_UNKNOWN, "__UNKNOWN")

typedef enum HttpMethod {
#define _METHOD(M, N) M,
    HTTP_METHOD_ENUM
#undef _METHOD
} HttpMethod;

#define HTTP_VERSION_ENUM                                                      \
    _VERSION(HTTP_VERSION_INVALID, "")                                         \
    _VERSION(HTTP_VERSION_10, "HTTP/1.0")                                      \
    _VERSION(HTTP_VERSION_11, "HTTP/1.1")                                      \
    _VERSION(HTCPCP_VERSION_10, "HTCPCP/1.0")                                  \
    _VERSION(HTTP_VERSION_UNKNOWN, "")

typedef enum HttpVersion {
#define _VERSION(V, S) V,
    HTTP_VERSION_ENUM
#undef _VERSION
} HttpVersion;

#define HTTP_CONTENT_TYPE_ENUM                                                 \
    _CTYPE(HTTP_CONTENT_TYPE_INVALID, "")                                      \
    _CTYPE(HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM,                         \
           "application/octet-stream")                                         \
    _CTYPE(HTTP_CONTENT_TYPE_APPLICATION_JSON, "application/json")             \
    _CTYPE(HTTP_CONTENT_TYPE_APPLICATION_PDF, "application/pdf")               \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_BMP, "image/bmp")                           \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_GIF, "image/gif")                           \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_ICO, "image/x-icon")                        \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_JPEG, "image/jpeg")                         \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_PNG, "image/png")                           \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_SVG, "image/svg+xml")                       \
    _CTYPE(HTTP_CONTENT_TYPE_IMAGE_WEBP, "image/webp")                         \
    _CTYPE(HTTP_CONTENT_TYPE_TEXT_CSS, "text/css")                             \
    _CTYPE(HTTP_CONTENT_TYPE_TEXT_JAVASCRIPT, "text/javascript")               \
    _CTYPE(HTTP_CONTENT_TYPE_TEXT_PLAIN, "text/plain")                         \
    _CTYPE(HTTP_CONTENT_TYPE_TEXT_HTML, "text/html")

typedef enum HttpContentType {
#define _CTYPE(T, S) T,
    HTTP_CONTENT_TYPE_ENUM
#undef _CTYPE
} HttpContentType;

#define HTTP_STATUS_ENUM                                                       \
    _STATUS(0, HTTP_STATUS_INVALID, "Invalid error")                           \
    _STATUS(200, HTTP_STATUS_OK, "OK")                                         \
    _STATUS(400, HTTP_STATUS_BAD_REQUEST, "Bad Request")                       \
    _STATUS(404, HTTP_STATUS_NOT_FOUND, "Not Found")                           \
    _STATUS(408, HTTP_STATUS_TIMEOUT, "Request Timeout")                       \
    _STATUS(413, HTTP_STATUS_PAYLOAD_TOO_LARGE, "Payload Too Large")           \
    _STATUS(414, HTTP_STATUS_URI_TOO_LONG, "URI Too Long")                     \
    _STATUS(418, HTTP_STATUS_IM_A_TEAPOT, "I'm a teapot")                      \
    _STATUS(431, HTTP_STATUS_HEADER_TOO_LARGE,                                 \
            "Request Header Fields Too Large")                                 \
    _STATUS(500, HTTP_STATUS_SERVER_ERROR, "Internal Server Error")            \
    _STATUS(501, HTTP_STATUS_NOT_IMPLEMENTED, "Not Implemented")               \
    _STATUS(505, HTTP_STATUS_VERSION_NOT_SUPPORTED,                            \
            "HTTP Version Not Supported")

typedef enum HttpStatus {
#define _STATUS(CODE, TYPE, REASON) TYPE,
    HTTP_STATUS_ENUM
#undef _STATUS
} HttpStatus;

#define HTTP_TRANSFER_ENCODING_ENUM                                            \
    _TENCODING(TRANSFER_ENCODING_IDENTITY, "identity")                         \
    _TENCODING(TRANSFER_ENCODING_CHUNKED, "chunked")

enum HttpTransferBits {
#define _TENCODING(E, S) _HTTP_##E,
    HTTP_TRANSFER_ENCODING_ENUM
#undef _TENCODING
};

typedef uint8_t HttpTransferEncoding;
#define _TENCODING(E, S)                                                       \
    static const HttpTransferEncoding HTTP_##E = 1 << (_HTTP_##E);
HTTP_TRANSFER_ENCODING_ENUM
#undef _TENCODING
static const HttpTransferEncoding HTTP_TRANSFER_ENCODING_INVALID = 0;

typedef enum HttpRequestResult {
    HTTP_REQUEST_ERROR,
    HTTP_REQUEST_NEED_DATA,
    HTTP_REQUEST_SENDING,
    HTTP_REQUEST_COMPLETE
} HttpRequestResult;

typedef enum HttpRequestStateResult {
    HTTP_REQUEST_STATE_ERROR     = HTTP_REQUEST_ERROR,
    HTTP_REQUEST_STATE_NEED_DATA = HTTP_REQUEST_NEED_DATA,
    HTTP_REQUEST_STATE_SENDING   = HTTP_REQUEST_SENDING,
    HTTP_REQUEST_STATE_BAIL      = HTTP_REQUEST_COMPLETE,
    HTTP_REQUEST_STATE_DONE
} HttpRequestStateResult;

HttpMethod           http_request_method_parse(A3CString str);
HttpVersion          http_version_parse(A3CString str);
HttpContentType      http_content_type_from_path(A3CString);
HttpTransferEncoding http_transfer_encoding_parse(A3CString value);

A3CString http_version_string(HttpVersion);
A3CString http_status_reason(HttpStatus);
uint16_t  http_status_code(HttpStatus status);
A3CString http_content_type_name(HttpContentType);
