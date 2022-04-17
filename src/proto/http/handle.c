/*
 * SHORT CIRCUIT: HTTP HANDLERS -- Pre-defined handlers for HTTP routes.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#ifdef __linux__
#define _GNU_SOURCE // For O_PATH.
#endif

#include <fcntl.h>
#include <sys/stat.h>

#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include <sc/http.h>
#include <sc/io.h>
#include <sc/route.h>
#include <sc/uri.h>

#include "connection.h"
#include "response.h"

static void sc_http_file_handle(void* conn, ScRouteData dir) {
    assert(conn);
    assert(dir.fd >= 0);

    ScHttpConnection* http = conn;
    A3CString         path = sc_uri_path_relative(&http->request.target);
    if (path.len == 0)
        path = A3_CS(".");

    SC_IO_RESULT(ScFd) maybe_file = sc_io_open_under(http->conn->coroutine, dir.fd, path, O_RDONLY);
    if (SC_IO_IS_ERR(maybe_file)) {
        A3_TRACE_F("Failed to open file \"" A3_S_F "\". " A3_S_F, A3_S_FORMAT(path),
                   A3_S_FORMAT(sc_io_error_to_string(maybe_file.err)));
        sc_http_response_error_send(&http->response, SC_HTTP_STATUS_NOT_FOUND);
        return;
    }

    sc_http_response_file_send(&http->response, maybe_file.ok);
}

ScRouter* sc_http_handle_file_serve(A3CString path) {
    assert(path.ptr && *path.ptr);

    ScFd dir = -1;
    A3_UNWRAPS(dir, open(a3_string_cstr(path), O_PATH));

    struct stat statbuf;
    A3_UNWRAPSD(fstat(dir, &statbuf));
    if (!S_ISDIR(statbuf.st_mode))
        A3_PANIC_FMT("Web root \"" A3_S_F "\" is not a directory.", A3_S_FORMAT(path));

    return sc_router_new(sc_http_file_handle, (ScRouteData) { .fd = dir });
}
