/*
 * SHORT CIRCUIT: HTTP -- HTTP protocol.
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

#include <a3/cpp.h>
#include <a3/str.h>
#include <a3/types.h>

#include <sc/forward.h>
#include <sc/route.h>

#include "sc/mime.h"

A3_H_BEGIN

#define SC_HTTP_VERSION_ENUM                                                                       \
    VERSION(SC_HTTP_VERSION_INVALID, "<INVALID VERSION>")                                          \
    VERSION(SC_HTTP_VERSION_09, "HTTP/0.9")                                                        \
    VERSION(SC_HTTP_VERSION_10, "HTTP/1.0")                                                        \
    VERSION(SC_HTTP_VERSION_11, "HTTP/1.1")                                                        \
    VERSION(SC_HTCPCP_VERSION_10, "HTCPCP/1.0")                                                    \
    VERSION(SC_HTTP_VERSION_UNKNOWN, "<UNKNOWN VERSION>")

typedef enum ScHttpVersion {
#define VERSION(V, S) V,
    SC_HTTP_VERSION_ENUM
#undef VERSION
} ScHttpVersion;

A3_EXPORT A3CString     sc_http_version_string(ScHttpVersion);
A3_EXPORT ScHttpVersion sc_http_version_parse(A3CString);

#define SC_HTTP_METHOD_ENUM                                                                        \
    METHOD(SC_HTTP_METHOD_INVALID, "<INVALID METHOD>")                                             \
    METHOD(SC_HTTP_METHOD_BREW, "BREW")                                                            \
    METHOD(SC_HTTP_METHOD_GET, "GET")                                                              \
    METHOD(SC_HTTP_METHOD_HEAD, "HEAD")                                                            \
    METHOD(SC_HTTP_METHOD_UNKNOWN, "<UNKNOWN METHOD>")

typedef enum ScHttpMethod {
#define METHOD(M, N) M,
    SC_HTTP_METHOD_ENUM
#undef METHOD
} ScHttpMethod;

#define SC_HTTP_STATUS_ENUM                                                                        \
    STATUS(0, SC_HTTP_STATUS_INVALID, "Invalid error")                                             \
    STATUS(200, SC_HTTP_STATUS_OK, "OK")                                                           \
    STATUS(400, SC_HTTP_STATUS_BAD_REQUEST, "Bad Request")                                         \
    STATUS(404, SC_HTTP_STATUS_NOT_FOUND, "Not Found")                                             \
    STATUS(408, SC_HTTP_STATUS_TIMEOUT, "Request Timeout")                                         \
    STATUS(413, SC_HTTP_STATUS_PAYLOAD_TOO_LARGE, "Payload Too Large")                             \
    STATUS(414, SC_HTTP_STATUS_URI_TOO_LONG, "URI Too Long")                                       \
    STATUS(418, SC_HTCPCP_STATUS_IM_A_TEAPOT, "I'm a teapot")                                      \
    STATUS(431, SC_HTTP_STATUS_HEADER_TOO_LARGE, "Request Header Fields Too Large")                \
    STATUS(500, SC_HTTP_STATUS_SERVER_ERROR, "Internal Server Error")                              \
    STATUS(501, SC_HTTP_STATUS_NOT_IMPLEMENTED, "Not Implemented")                                 \
    STATUS(505, SC_HTTP_STATUS_VERSION_NOT_SUPPORTED, "HTTP Version Not Supported")

typedef enum ScHttpStatus {
#define STATUS(CODE, TYPE, REASON) TYPE = (CODE),
    SC_HTTP_STATUS_ENUM
#undef STATUS
} ScHttpStatus;

A3_EXPORT A3CString sc_http_status_reason(ScHttpStatus);

#define SC_HTTP_CONNECTION_TYPE_ENUM                                                               \
    CTYPE(SC_HTTP_CONNECTION_TYPE_CLOSE, "Close")                                                  \
    CTYPE(SC_HTTP_CONNECTION_TYPE_KEEP_ALIVE, "Keep-Alive")                                        \
    CTYPE(SC_HTTP_CONNECTION_TYPE_UNSPECIFIED, "")                                                 \
    CTYPE(SC_HTTP_CONNECTION_TYPE_INVALID, "")

typedef enum ScHttpConnectionType {
#define CTYPE(TY, S) TY,
    SC_HTTP_CONNECTION_TYPE_ENUM
#undef CTYPE
} ScHttpConnectionType;

#define SC_HTTP_TRANSFER_ENCODING_ENUM ENCODING(SC_HTTP_TRANSFER_ENCODING_CHUNKED, "chunked")

typedef enum ScHttpTransferBits {
#define ENCODING(E, S) E##_BITS,
    SC_HTTP_TRANSFER_ENCODING_ENUM
#undef ENCODING
} ScHttpTransferBits;

#define ENCODING(E, S) E = 1 << (E##_BITS),
typedef enum ScHttpTransferEncoding { SC_HTTP_TRANSFER_ENCODING_ENUM } ScHttpTransferEncoding;
#undef ENCODING

#define SC_HTTP_TRANSFER_ENCODING_IDENTITY 0
#define SC_HTTP_TRANSFER_ENCODING_INVALID  (~0U)

#define SC_HTTP_CONTENT_LENGTH_UNSPECIFIED (-1LL)
#define SC_HTTP_CONTENT_LENGTH_INVALID     (-2LL)

#define SC_HTTP_EOL   A3_CS("\r\n")
#define SC_HTTP_EOL_2 A3_CS("\r\n\r\n")

bool sc_http_connection_keep_alive(ScHttpConnection*);

A3_EXPORT void sc_http_response_file_prep(ScHttpResponse*, ScFd file, ScMimeType);

A3_EXPORT void sc_http_response_send(ScHttpResponse*);
#define SC_HTTP_CLOSE true
#define SC_HTTP_KEEP  false
A3_EXPORT void sc_http_response_error_prep_and_send(ScHttpResponse*, ScHttpStatus, bool close);

A3_EXPORT ScRouter* sc_http_handle_file_serve(A3CString path);

A3_H_END
