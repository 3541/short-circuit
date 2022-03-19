/*
 * SHORT CIRCUIT: HTTP PARSE -- Incremental HTTP request parser.
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

#include "http/parse.h"

#include <assert.h>
#include <liburing.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "config_runtime.h"
#include "connection.h"
#include "forward.h"
#include "http/connection.h"
#include "http/headers.h"
#include "http/request.h"
#include "http/response.h"
#include "http/types.h"
#include "uri.h"

static inline A3Buffer* http_request_recv_buf(HttpRequest* req) {
    assert(req);

    return &http_request_connection(req)->conn.recv_buf;
}

// Try to parse the first line of the HTTP request.
HttpRequestStateResult http_request_first_line_parse(HttpRequest* req, struct io_uring* uring) {
    assert(req);
    assert(uring);

    A3Buffer*       buf  = http_request_recv_buf(req);
    HttpConnection* conn = http_request_connection(req);
    HttpResponse*   resp = http_request_response(req);

    // If no CRLF has appeared so far, and the length of the data is
    // permissible, bail and wait for more.
    if (!a3_buf_memmem(buf, HTTP_NEWLINE).ptr) {
        if (a3_buf_len(buf) < HTTP_REQUEST_LINE_MAX_LENGTH)
            return HTTP_REQUEST_STATE_NEED_DATA;
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    conn->method = http_request_method_parse(
        A3_S_CONST(a3_buf_token_next(buf, A3_CS(" \r\n"), A3_PRES_END_NO)));
    switch (conn->method) {
    case HTTP_METHOD_INVALID:
        A3_TRACE("Got an invalid method.");
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case HTTP_METHOD_UNKNOWN:
        A3_TRACE("Got an unknown method.");
        A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_NOT_IMPLEMENTED,
                                              HTTP_RESPONSE_ALLOW),
                   HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    default:
        // Valid methods.
        break;
    }

    A3String target_str = a3_buf_token_next(buf, A3_CS(" \r\n"), A3_PRES_END_NO);
    if (!target_str.ptr)
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    switch (uri_parse(&req->target, target_str)) {
    case URI_PARSE_ERROR:
    case URI_PARSE_BAD_URI:
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_ALLOW),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case URI_PARSE_TOO_LONG:
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    default:
        break;
    }

    req->target_path = uri_path_if_contained(&req->target, CONFIG.web_root);
    if (!req->target_path.ptr)
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    // Need to only eat one "\r\n".
    conn->version =
        http_version_parse(A3_S_CONST(a3_buf_token_next(buf, HTTP_NEWLINE, A3_PRES_END_YES)));
    if (!a3_buf_consume(buf, HTTP_NEWLINE) || conn->version == HTTP_VERSION_INVALID ||
        conn->version == HTTP_VERSION_UNKNOWN ||
        (conn->version == HTCPCP_VERSION_10 && conn->method != HTTP_METHOD_BREW)) {
        A3_TRACE("Got a bad HTTP version.");
        A3_RET_MAP(http_response_error_submit(resp, uring,
                                              (conn->version == HTTP_VERSION_INVALID)
                                                  ? HTTP_STATUS_BAD_REQUEST
                                                  : HTTP_STATUS_VERSION_NOT_SUPPORTED,
                                              HTTP_RESPONSE_CLOSE),
                   HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    } else if (conn->version == HTTP_VERSION_10) {
        // HTTP/1.0 is 'Connection: Close' by default.
        conn->connection_type = HTTP_CONNECTION_TYPE_CLOSE;
    }

    conn->state = HTTP_CONNECTION_PARSED_FIRST_LINE;

    return HTTP_REQUEST_STATE_DONE;
}

// Try to ingest all the headers.
HttpRequestStateResult http_request_headers_add(HttpRequest* req, struct io_uring* uring) {
    assert(req);
    assert(uring);

    A3Buffer*     buf  = http_request_recv_buf(req);
    HttpResponse* resp = http_request_response(req);

    if (!a3_buf_memmem(buf, HTTP_NEWLINE).ptr) {
        if (a3_buf_len(buf) < HTTP_REQUEST_HEADER_MAX_LENGTH)
            return HTTP_REQUEST_STATE_NEED_DATA;
        A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_HEADER_TOO_LARGE,
                                              HTTP_RESPONSE_CLOSE),
                   HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    if (!a3_buf_consume(buf, HTTP_NEWLINE)) {
        while (buf->data.ptr[buf->head] != '\r' && buf->head != buf->tail) {
            if (!a3_buf_memmem(buf, HTTP_NEWLINE).ptr)
                return HTTP_REQUEST_STATE_NEED_DATA;

            A3CString name  = A3_S_CONST(a3_buf_token_next(buf, A3_CS(": "), A3_PRES_END_NO));
            A3CString value = A3_S_CONST(a3_buf_token_next(buf, HTTP_NEWLINE, A3_PRES_END_YES));
            A3_TRYB_MAP(a3_buf_consume(buf, HTTP_NEWLINE), HTTP_REQUEST_STATE_ERROR);

            // RFC7230 § 5.4: Invalid field-value -> 400.
            if (!name.ptr || !value.ptr)
                A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST,
                                                      HTTP_RESPONSE_CLOSE),
                           HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

            if (!http_header_add(&req->headers, name, value))
                A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_SERVER_ERROR,
                                                      HTTP_RESPONSE_CLOSE),
                           HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
        }

        if (!a3_buf_consume(buf, HTTP_NEWLINE))
            return HTTP_REQUEST_STATE_NEED_DATA;
    }

    http_request_connection(req)->state = HTTP_CONNECTION_ADDED_HEADERS;

    return HTTP_REQUEST_STATE_DONE;
}

// Parse all the headers.
HttpRequestStateResult http_request_headers_parse(HttpRequest* req, struct io_uring* uring) {
    assert(req);
    assert(uring);

    HttpConnection* conn    = http_request_connection(req);
    HttpResponse*   resp    = http_request_response(req);
    HttpHeaders*    headers = &req->headers;

    conn->connection_type = http_header_connection(headers);
    if (conn->connection_type == HTTP_CONNECTION_TYPE_UNSPECIFIED)
        conn->connection_type = (conn->version == HTTP_VERSION_10)
                                    ? HTTP_CONNECTION_TYPE_CLOSE
                                    : HTTP_CONNECTION_TYPE_KEEP_ALIVE;
    else if (conn->connection_type == HTTP_CONNECTION_TYPE_INVALID) {
        conn->connection_type = (conn->version == HTTP_VERSION_10)
                                    ? HTTP_CONNECTION_TYPE_CLOSE
                                    : HTTP_CONNECTION_TYPE_KEEP_ALIVE;
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    req->host = A3_S_CONST(http_header_get(headers, A3_CS("Host")));
    // RFC7230 § 5.4: more than one Host header -> 400.
    if (req->host.ptr && a3_string_rchr(req->host, ',').ptr) {
        req->host = A3_CS_NULL;
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }
    // ibid. HTTP/1.1 messages must have a Host header.
    if (conn->version == HTTP_VERSION_11 && !req->host.ptr)
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    req->transfer_encodings = http_header_transfer_encodings(headers);
    if (!req->transfer_encodings)
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    // RFC7230 § 3.3.3, step 3: Transfer-Encoding without chunked is invalid in
    // a request, and the server MUST respond with a 400.
    if ((req->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) &&
        !(req->transfer_encodings & HTTP_TRANSFER_ENCODING_CHUNKED))
        A3_RET_MAP(
            http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST, HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    // TODO: Support other transfer encodings.
    if ((req->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY))
        A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_NOT_IMPLEMENTED,
                                              HTTP_RESPONSE_CLOSE),
                   HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    // ibid. Transfer-Encoding overrides Content-Length.
    if (!(req->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY)) {
        req->content_length = http_header_content_length(headers);
        if (req->content_length == HTTP_CONTENT_LENGTH_INVALID)
            A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_BAD_REQUEST,
                                                  HTTP_RESPONSE_CLOSE),
                       HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
        if (req->content_length != HTTP_CONTENT_LENGTH_UNSPECIFIED &&
            (size_t)req->content_length > HTTP_REQUEST_CONTENT_MAX_LENGTH)
            A3_RET_MAP(http_response_error_submit(resp, uring, HTTP_STATUS_PAYLOAD_TOO_LARGE,
                                                  HTTP_RESPONSE_CLOSE),
                       HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
        // ibid. step 6: default to Content-Length of 0.
        if (req->content_length == HTTP_CONTENT_LENGTH_UNSPECIFIED)
            req->content_length = 0;
    }

    conn->state = HTTP_CONNECTION_PARSED_HEADERS;

    return HTTP_REQUEST_STATE_DONE;
}
