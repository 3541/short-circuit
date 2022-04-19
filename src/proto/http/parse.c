/*
 * SHORT CIRCUIT: HTTP PARSE -- HTTP request parser.
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

#include "parse.h"

#include <assert.h>
#include <stdbool.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/str.h>

#include <sc/http.h>

#include "config.h"
#include "connection.h"
#include "proto/http/headers.h"
#include "request.h"
#include "response.h"
#include "sc/uri.h"

static A3Buffer* sc_http_request_recv_buf(ScHttpRequest* req) {
    assert(req);

    return &sc_http_request_connection(req)->conn->recv_buf;
}

static ScHttpMethod sc_http_request_method_parse(A3CString str) {
#define METHOD(M, N) { M, A3_CS(N) },
    static struct {
        ScHttpMethod method;
        A3CString    name;
    } const HTTP_METHODS[] = { SC_HTTP_METHOD_ENUM };
#undef METHOD

    if (!str.ptr || !*str.ptr || !a3_string_isascii(str))
        return SC_HTTP_METHOD_INVALID;

    for (size_t i = 0; i < sizeof(HTTP_METHODS) / sizeof(HTTP_METHODS[0]); i++) {
        if (a3_string_cmpi(str, HTTP_METHODS[i].name) == 0)
            return HTTP_METHODS[i].method;
    }

    return SC_HTTP_METHOD_UNKNOWN;
}

static bool sc_http_request_first_line_parse(ScHttpRequest* req) {
    assert(req);

    A3_TRACE("Parsing HTTP request first line.");

    A3Buffer*         buf  = sc_http_request_recv_buf(req);
    ScHttpConnection* conn = sc_http_request_connection(req);
    ScHttpResponse*   resp = sc_http_request_response(req);

    if (SC_IO_IS_ERR(
            sc_connection_recv_until(conn->conn, SC_HTTP_EOL, SC_HTTP_REQUEST_LINE_MAX_LENGTH))) {
        A3_WARN("recv failed");
        sc_connection_close(conn->conn);
        return false;
    }

    if (a3_buf_len(buf) >= SC_HTTP_REQUEST_LINE_MAX_LENGTH) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_URI_TOO_LONG, SC_HTTP_CLOSE);
        return false;
    }

    A3CString method = A3_S_CONST(a3_buf_token_next(buf, A3_CS(" \r\n"), A3_PRES_END_NO));
    req->method      = sc_http_request_method_parse(method);
    switch (req->method) {
    case SC_HTTP_METHOD_INVALID:
        A3_TRACE_F("Invalid HTTP method \"" A3_S_F "\".", A3_S_FORMAT(method));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    case SC_HTTP_METHOD_UNKNOWN:
        A3_TRACE_F("Unimplemented HTTP method \"" A3_S_F "\".", A3_S_FORMAT(method));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_IMPLEMENTED, SC_HTTP_KEEP);
        return false;
    default:
        break;
    }

    A3String target = a3_buf_token_next_copy(buf, A3_CS(" \r\n"), A3_PRES_END_NO);
    if (!target.ptr) {
        A3_TRACE("Missing URI.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }

    if (sc_uri_parse(&req->target, target) != SC_URI_PARSE_OK) {
        A3_TRACE_F("Bad URI \"" A3_S_F "\".", A3_S_FORMAT(target));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }

    // Only eat one EOL, in order to determine whether to expect headers.
    A3CString version = A3_S_CONST(a3_buf_token_next(buf, SC_HTTP_EOL, A3_PRES_END_YES));
    conn->version     = sc_http_version_parse(version);
    // There must be at least one EOL. The version must be valid. If on HTTP/0.9, there must be no
    // headers.
    if (!a3_buf_consume(buf, SC_HTTP_EOL) || conn->version == SC_HTTP_VERSION_INVALID) {
        A3_TRACE_F("Bad HTTP version \"", A3_S_F, "\".", A3_S_FORMAT(version));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }
    if (conn->version == SC_HTTP_VERSION_UNKNOWN) {
        A3_TRACE_F("Unknown HTTP version \"", A3_S_F, "\".", A3_S_FORMAT(version));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_VERSION_NOT_SUPPORTED, SC_HTTP_KEEP);
        return false;
    }

    // HTTP/1.0 and HTTP/0.9 are Connection: Close by default.
    if (conn->version <= SC_HTTP_VERSION_10)
        conn->connection_type = SC_HTTP_CONNECTION_TYPE_CLOSE;

    return true;
}

static bool sc_http_request_headers_recv(ScHttpRequest* req) {
    assert(req);

    A3_TRACE("Receiving HTTP headers.");

    A3Buffer*         buf  = sc_http_request_recv_buf(req);
    ScHttpConnection* conn = sc_http_request_connection(req);
    ScHttpResponse*   resp = sc_http_request_response(req);

    SC_IO_TRY_MAP(
        sc_connection_recv_until(conn->conn, SC_HTTP_EOL,
                                 SC_HTTP_REQUEST_LINE_MAX_LENGTH + SC_HTTP_HEADER_MAX_LENGTH),
        false);

    // No headers.
    if (a3_buf_consume(buf, SC_HTTP_EOL))
        return true;

    // No headers allowed for HTTP/0.9.
    if (conn->version == SC_HTTP_VERSION_09) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }

    SC_IO_TRY_MAP(sc_connection_recv_until(conn->conn, SC_HTTP_EOL_2, buf->max_cap), false);

    while (buf->data.ptr[buf->head] != '\r' && a3_buf_len(buf) > 0) {
        A3CString name  = A3_S_CONST(a3_buf_token_next(buf, A3_CS(": "), A3_PRES_END_NO));
        A3CString value = A3_S_CONST(a3_buf_token_next(buf, SC_HTTP_EOL, A3_PRES_END_YES));
        A3_TRYB(a3_buf_consume(buf, SC_HTTP_EOL));

        // RFC7230 § 5.4: Invalid field-value -> 400.
        if (!name.ptr || !value.ptr) {
            sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
            return false;
        }

        if (!sc_http_header_add(&req->headers, name, value)) {
            sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
            return false;
        }
    }

    if (!a3_buf_consume(buf, SC_HTTP_EOL)) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }

    return true;
}

static bool sc_http_request_headers_parse(ScHttpRequest* req) {
    assert(req);

    A3_TRACE("Parsing HTTP headers.");

    ScHttpConnection* conn = sc_http_request_connection(req);
    ScHttpResponse*   resp = sc_http_request_response(req);

    conn->connection_type = sc_http_header_connection(&req->headers);
    if (conn->connection_type == SC_HTTP_CONNECTION_TYPE_INVALID) {
        conn->connection_type = SC_HTTP_CONNECTION_TYPE_CLOSE;
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }
    if (conn->connection_type == SC_HTTP_CONNECTION_TYPE_UNSPECIFIED) {
        conn->connection_type = (conn->version <= SC_HTTP_VERSION_10)
                                    ? SC_HTTP_CONNECTION_TYPE_CLOSE
                                    : SC_HTTP_CONNECTION_TYPE_KEEP_ALIVE;
    }

    req->host = A3_S_CONST(sc_http_header_get(&req->headers, A3_CS("Host")));
    // RFC7230 § 5.4: Invalid field-value -> 400.
    if (req->host.ptr && a3_string_rchr(req->host, ',').ptr) {
        req->host = A3_CS_NULL;
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }
    // ibid. HTTP/1.1 messages must have a Host header.
    if (conn->version >= SC_HTTP_VERSION_11 && !req->host.ptr) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }

    req->transfer_encodings = sc_http_header_transfer_encoding(&req->headers);
    if (req->transfer_encodings == SC_HTTP_TRANSFER_ENCODING_INVALID) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }
    // RFC7230 § 3.3.3, step 3: Transfer-Encoding without chunked is invalid in a request, and the
    // server MUST respond with a 400.
    if (req->transfer_encodings != SC_HTTP_TRANSFER_ENCODING_IDENTITY &&
        !(req->transfer_encodings & SC_HTTP_TRANSFER_ENCODING_CHUNKED)) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
        return false;
    }
    // TODO: Support other transfer encodings.
    if (req->transfer_encodings != SC_HTTP_TRANSFER_ENCODING_IDENTITY) {
        sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_IMPLEMENTED, SC_HTTP_CLOSE);
        return false;
    }

    // ibid. Transfer-Encoding overrides Content-Length.
    if (req->transfer_encodings == SC_HTTP_TRANSFER_ENCODING_IDENTITY) {
        req->content_length = sc_http_header_content_length(&req->headers);

        if (req->content_length == SC_HTTP_CONTENT_LENGTH_INVALID) {
            sc_http_response_error_send(resp, SC_HTTP_STATUS_BAD_REQUEST, SC_HTTP_CLOSE);
            return false;
        }
        if (req->content_length != SC_HTTP_CONTENT_LENGTH_UNSPECIFIED &&
            req->content_length > SC_HTTP_REQUEST_CONTENT_MAX_LENGTH) {
            sc_http_response_error_send(resp, SC_HTTP_STATUS_PAYLOAD_TOO_LARGE, SC_HTTP_CLOSE);
            return false;
        }
        // ibid. step 6: default to Content-Length of 0.
        if (req->content_length == SC_HTTP_CONTENT_LENGTH_UNSPECIFIED)
            req->content_length = 0;
    }

    return true;
}

bool sc_http_request_parse(ScHttpRequest* req) {
    assert(req);

    A3_TRACE("Parsing HTTP request.");

    A3_TRYB(sc_http_request_first_line_parse(req));
    A3_TRYB(sc_http_request_headers_recv(req));
    A3_TRYB(sc_http_request_headers_parse(req));

    A3_TRACE("Parsed HTTP request.");
    return true;
}
