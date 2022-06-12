/*
 * SHORT CIRCUIT: HTTP CONNECTION -- HTTP-specific layer on top of a connection.
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

#include "connection.h"

#include <assert.h>

#include <a3/log.h>

#include <sc/connection.h>
#include <sc/http.h>
#include <sc/listen.h>

#include "proto/http/request.h"
#include "proto/http/response.h"

#define VERSION(V, S) [V] = A3_CS(S),
static A3CString const SC_HTTP_VERSIONS[] = { SC_HTTP_VERSION_ENUM };
#undef VERSION

A3CString sc_http_version_string(ScHttpVersion version) {
    assert(version >= 0 && version < sizeof(SC_HTTP_VERSIONS) / sizeof(SC_HTTP_VERSIONS[0]));
    return SC_HTTP_VERSIONS[version];
}

A3CString sc_http_status_reason(ScHttpStatus status) {
#define STATUS(C, S, R) [C]                         = A3_CS(R),
    static A3CString const SC_HTTP_STATUS_REASONS[] = { SC_HTTP_STATUS_ENUM };
#undef STATUS

    assert(status >= 0 &&
           status < sizeof(SC_HTTP_STATUS_REASONS) / sizeof(SC_HTTP_STATUS_REASONS[0]));

    return SC_HTTP_STATUS_REASONS[status];
}

ScHttpVersion sc_http_version_parse(A3CString str) {
    if (!str.ptr || !*str.ptr)
        return SC_HTTP_VERSION_09;

    if (!a3_string_isascii(str))
        return SC_HTTP_VERSION_INVALID;

    for (ScHttpVersion v = SC_HTTP_VERSION_10; v < SC_HTTP_VERSION_UNKNOWN; v++) {
        if (a3_string_cmpi(str, SC_HTTP_VERSIONS[v]) == 0)
            return v;
    }

    return SC_HTTP_VERSION_UNKNOWN;
}

void sc_http_connection_handle(ScConnection* conn) {
    assert(conn);

    A3_TRACE("Handling HTTP connection.");
    ScHttpConnection http = { .conn            = conn,
                              .version         = SC_HTTP_VERSION_11,
                              .connection_type = SC_HTTP_CONNECTION_TYPE_KEEP_ALIVE };
    sc_http_request_init(&http.request);
    sc_http_response_init(&http.response);

    SC_IO_RESULT(size_t) rc = SC_IO_OK(size_t, 0);
    do {
        sc_http_request_handle(&http.request);
        sc_http_request_reset(&http.request);
        sc_http_response_reset(&http.response);
        sc_connection_timeout_reset(http.conn);
    } while (http.connection_type == SC_HTTP_CONNECTION_TYPE_KEEP_ALIVE && conn->socket > 0 &&
             SC_IO_IS_OK(rc = sc_connection_recv(conn)) && rc.ok > 0);

    if (SC_IO_IS_ERR(rc)) {
        switch (rc.err) {
        case SC_IO_EOF:
            break;
        case SC_IO_TIMEOUT:
            sc_http_response_error_prep_and_send(&http.response, SC_HTTP_STATUS_TIMEOUT,
                                                 SC_HTTP_CLOSE);
            break;
        default:
            SC_IO_UNWRAP(rc);
        }
    }

    sc_http_response_destroy(&http.response);
    sc_http_request_destroy(&http.request);
}

bool sc_http_connection_keep_alive(ScHttpConnection* conn) {
    assert(conn);

    return conn->connection_type == SC_HTTP_CONNECTION_TYPE_KEEP_ALIVE;
}
