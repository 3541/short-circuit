/*
 * SHORT CIRCUIT: IO -- IO event loop.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#include "io.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/coroutine.h>
#include <sc/io.h>

#include "backend/backend.h"

static volatile sig_atomic_t SC_TERMINATE = false;

static void sc_signal_handler(int signum) {
    (void)signum;

    SC_TERMINATE = true;
}

A3CString sc_io_error_to_string(ScIoError error) {
#define ERR(N, V, S) [-(N)]         = A3_CS(S),
    static A3CString const ERRORS[] = { SC_IO_ERROR_ENUM };
#undef ERR
    assert(error < 0 && (size_t)-error < sizeof(ERRORS) / sizeof(ERRORS[0]));

    return ERRORS[-error];
}

static void sc_io_event_loop_pump(ScEventLoop* ev) {
    assert(ev);

    sc_io_backend_pump(&ev->backend, (struct timespec) { .tv_sec = 1000, .tv_nsec = 0 });
}

ScEventLoop* sc_io_event_loop_new() {
    A3_UNWRAPNI(ScEventLoop*, ret, calloc(1, sizeof(*ret)));

    sc_io_backend_init(&ret->backend);
    return ret;
}

void sc_io_event_loop_free(ScEventLoop* ev) {
    assert(ev);

    sc_io_backend_destroy(&ev->backend);
    free(ev);
}

void sc_io_event_loop_run(ScCoMain* co) {
    assert(co);

    if (signal(SIGINT, sc_signal_handler) == SIG_ERR) {
        A3_ERRNO(errno, "signal");
        A3_PANIC("Failed to register a signal handler.");
    }

    ScEventLoop* ev = sc_co_main_event_loop(co);

    A3_TRACE("Starting event loop.");
    while (!SC_TERMINATE && sc_co_count(co) > 0) {
        sc_co_main_pending_resume(co);
        sc_io_event_loop_pump(ev);
    }
}

SC_IO_RESULT(size_t)
sc_io_read(ScFd fd, A3String dst, size_t count, off_t offset) {
    assert(fd >= 0);
    assert(dst.ptr);
    assert(count);

    size_t to_read = MIN(count, dst.len);
    size_t left    = to_read;

    while (left) {
        SC_IO_RESULT(size_t) res = sc_io_read_raw(fd, dst, count, offset);
        if (SC_IO_IS_ERR(res)) {
            if (res.err == SC_IO_EOF)
                break;
            return res;
        }

        left -= res.ok;
        if (offset >= 0)
            offset += (off_t)res.ok;
        dst = a3_string_offset(dst, res.ok);
    }

    return SC_IO_OK(size_t, to_read - left);
}

SC_IO_RESULT(size_t)
sc_io_writev(ScFd fd, struct iovec* iov, unsigned count) {
    assert(fd >= 0);
    assert(iov);
    assert(count);
    assert(count <= INT_MAX);

    size_t to_write = 0;
    for (size_t i = 0; i < count; i++)
        to_write += iov[i].iov_len;
    size_t left = to_write;

    while (left) {
        size_t res = SC_IO_TRY(size_t, sc_io_writev_raw(fd, iov, count));
        left -= res;

        if (!left || !res)
            break;

        size_t iovecs_done = 0;
        for (; res && iovecs_done < count; iovecs_done++) {
            if (res < iov[iovecs_done].iov_len) {
                iov[iovecs_done].iov_base += res;
                iov[iovecs_done].iov_len -= res;
                break;
            }

            res -= iov[iovecs_done].iov_len;
        }

        iov += iovecs_done;
        count -= (unsigned)iovecs_done;
    }

    return SC_IO_OK(size_t, to_write);
}
