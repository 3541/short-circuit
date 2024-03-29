/*
 * SHORT CIRCUIT: HTTP REQUEST -- HTTP request handlers.
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

#include "http/request.h"

#include <assert.h>
#include <liburing.h>

#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "forward.h"
#include "http/connection.h"
#include "http/parse.h"
#include "http/response.h"
#include "http/types.h"
#include "uri.h"

HttpConnection* http_request_connection(HttpRequest* req) {
    assert(req);

    return A3_CONTAINER_OF(req, HttpConnection, request);
}

HttpResponse* http_request_response(HttpRequest* req) {
    return &http_request_connection(req)->response;
}

// TODO: Perhaps handle things other than static files.
static HttpRequestStateResult http_request_get_head_handle(HttpRequest*     req,
                                                           struct io_uring* uring) {
    assert(req);
    assert(uring);

    // TODO: GET things other than static files.
    A3_RET_MAP(http_response_file_submit(http_request_response(req), uring),
               HTTP_REQUEST_STATE_DONE, HTTP_REQUEST_STATE_ERROR);
}

// Do whatever is appropriate for the parsed method.
static HttpRequestStateResult http_request_method_handle(HttpRequest* req, struct io_uring* uring) {
    assert(req);
    assert(uring);

    HttpConnection* conn = http_request_connection(req);

    switch (conn->method) {
    case HTTP_METHOD_HEAD:
    case HTTP_METHOD_GET:
        return http_request_get_head_handle(req, uring);
    case HTTP_METHOD_BREW:
        conn->version = HTCPCP_VERSION_10;
        A3_RET_MAP(http_response_error_submit(http_request_response(req), uring,
                                              HTCPCP_STATUS_IM_A_TEAPOT, HTTP_RESPONSE_ALLOW),
                   HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case HTTP_METHOD_INVALID:
    case HTTP_METHOD_UNKNOWN:
        A3_UNREACHABLE();
    }

    A3_UNREACHABLE();
}

void http_request_init(HttpRequest* req) {
    assert(req);

    memset(req, 0, sizeof(*req));

    req->transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    req->content_length     = HTTP_CONTENT_LENGTH_UNSPECIFIED;
    http_headers_init(&req->headers);
}

void http_request_reset(HttpRequest* req) {
    assert(req);

    if (uri_is_initialized(&req->target))
        uri_free(&req->target);
    if (req->target_path.ptr)
        a3_string_free(&req->target_path);
    http_headers_destroy(&req->headers);

    memset(req, 0, sizeof(*req));
}

// Try to parse as much of the HTTP request as possible.
bool http_request_handle(Connection* connection, struct io_uring* uring, bool success,
                         int32_t status) {
    assert(connection);
    assert(uring);
    assert(success);
    (void)success;
    (void)status;

    HttpConnection* conn = connection_http(connection);
    // TODO: Get more data here instead of returning the error up.

    HttpRequestStateResult rc = HTTP_REQUEST_STATE_ERROR;

    // Go through as many states as possible with the data currently loaded.
    switch (conn->state) {
    case HTTP_CONNECTION_INIT:
        if ((rc = http_request_first_line_parse(&conn->request, uring)) != HTTP_REQUEST_STATE_DONE)
            break;
        // fallthrough
    case HTTP_CONNECTION_PARSED_FIRST_LINE:
        if ((rc = http_request_headers_add(&conn->request, uring)) != HTTP_REQUEST_STATE_DONE)
            break;
        // fallthrough
    case HTTP_CONNECTION_ADDED_HEADERS:
        if ((rc = http_request_headers_parse(&conn->request, uring)) != HTTP_REQUEST_STATE_DONE)
            break;
        // fallthrough
    case HTTP_CONNECTION_PARSED_HEADERS:
        if ((rc = http_request_method_handle(&conn->request, uring)) != HTTP_REQUEST_STATE_DONE)
            break;
        // fallthrough
    case HTTP_CONNECTION_OPENING_FILE:
    case HTTP_CONNECTION_RESPONDING:
    case HTTP_CONNECTION_CLOSING:
        return HTTP_REQUEST_COMPLETE;
    }

    switch (rc) {
    case HTTP_REQUEST_STATE_BAIL:
    case HTTP_REQUEST_STATE_DONE:
    case HTTP_REQUEST_STATE_SENDING:
        return true;
    case HTTP_REQUEST_STATE_ERROR:
        return false;
    case HTTP_REQUEST_STATE_NEED_DATA:
        break;
    }

    // Request more data.
    return connection_recv_submit(connection, uring, http_request_handle);
}
