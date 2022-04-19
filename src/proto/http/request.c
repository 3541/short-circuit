/*
 * SHORT CIRCUIT: HTTP REQUEST -- HTTP request handling.
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

#include "request.h"

#include <assert.h>
#include <stdbool.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/connection.h>
#include <sc/listen.h>
#include <sc/route.h>

#include "connection.h"
#include "headers.h"
#include "parse.h"

static ScRouter* sc_http_request_router(ScHttpRequest* req) {
    return sc_listener_router(sc_http_request_connection(req)->conn->listener);
}

void sc_http_request_init(ScHttpRequest* req) {
    assert(req);

    sc_http_request_reset(req);
}

void sc_http_request_reset(ScHttpRequest* req) {
    req->method             = SC_HTTP_METHOD_UNKNOWN;
    req->target.data        = A3_S_NULL;
    req->host               = A3_CS_NULL;
    req->transfer_encodings = SC_HTTP_TRANSFER_ENCODING_IDENTITY;
    req->content_length     = SC_HTTP_CONTENT_LENGTH_UNSPECIFIED;
    if (req->target.data.ptr)
        a3_string_free(&req->target.data);

    if (sc_http_headers_count(&req->headers) > 0)
        sc_http_headers_destroy(&req->headers);
    sc_http_headers_init(&req->headers);
}

void sc_http_request_destroy(ScHttpRequest* req) {
    assert(req);

    if (req->target.data.ptr)
        a3_string_free(&req->target.data);
    sc_http_headers_destroy(&req->headers);
}

void sc_http_request_handle(ScHttpRequest* req) {
    assert(req);

    if (!sc_http_request_parse(req))
        return;

    A3_TRACE("Handling HTTP request.");

    ScHttpResponse* resp = sc_http_request_response(req);

    switch (req->method) {
    case SC_HTTP_METHOD_HEAD:
    case SC_HTTP_METHOD_GET:
        sc_router_dispatch(sc_http_request_router(req), sc_http_request_connection(req));
        break;
    case SC_HTTP_METHOD_BREW:
        sc_http_request_connection(req)->version = SC_HTCPCP_VERSION_10;
        sc_http_response_error_send(resp, SC_HTCPCP_STATUS_IM_A_TEAPOT, SC_HTTP_KEEP);
        break;
    case SC_HTTP_METHOD_UNKNOWN:
        sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_IMPLEMENTED, SC_HTTP_CLOSE);
        break;
    default:
        A3_UNREACHABLE();
    }
}

ScHttpConnection* sc_http_request_connection(ScHttpRequest* req) {
    assert(req);

    return A3_CONTAINER_OF(req, ScHttpConnection, request);
}

ScHttpResponse* sc_http_request_response(ScHttpRequest* req) {
    assert(req);

    return &sc_http_request_connection(req)->response;
}
