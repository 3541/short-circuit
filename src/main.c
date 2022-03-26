/*
 * SHORT CIRCUIT -- A high-performance HTTP server for Linux, built on io_uring.
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
 *
 * Note: This whole file is a bit of a hack at the moment, and should probably
 * be regarded more as a test harness for development purposes than an actual
 * final interface.
 */

#include <fcntl.h>
#include <stdio.h>

#include <a3/log.h>
#include <a3/str.h>

#include <sc/coroutine.h>
#include <sc/io.h>
#include <sc/listen.h>

#include "config.h"
#include "sc/forward.h"

ssize_t cof(ScCoroutine* self, void* data) {
    (void)data;

    int res = sc_io_openat(self, AT_FDCWD, A3_CS("build.ninja"), O_RDONLY);
    if (res < 0) {
        A3_ERRNO(-res, "failed to open file");
        return -1;
    }
    ScFd fd = res;

    A3String buf  = a3_string_alloc(512);
    ssize_t  read = sc_io_read(self, fd, buf, 0);
    if (read < 0) {
        A3_ERRNO(-read, "failed to read file");
        return -1;
    }

    printf("%s\n", buf.ptr);

    a3_string_free(&buf);
    return 0;
}

static ssize_t handler(ScHttpConnection* conn) {
    assert(conn);

    printf("Handler.\n");

    return 0;
}

int main(void) {
    a3_log_init(stderr, A3_LOG_TRACE);

    ScCoCtx*     main_ctx = sc_co_main_ctx_new();
    ScEventLoop* ev       = sc_io_event_loop_new();

    ScListener* listener = sc_listener_http_new(SC_DEFAULT_LISTEN_PORT, handler);
    sc_listener_start(listener, main_ctx, ev);

    sc_io_event_loop_run(ev);

    sc_io_event_loop_free(ev);
    sc_co_main_ctx_free(main_ctx);
}
