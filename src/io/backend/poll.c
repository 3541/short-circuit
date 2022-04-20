/*
 * SHORT CIRCUIT: POLL -- poll event backend.
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

// The below defines fix editor highlighting when configured for another backend. This doesn't break
// compilation, since this file is only ever built by inclusion from backend.c.
#ifndef SC_IO_BACKEND_POLL
#undef SC_IO_BACKEND_URING
#define SC_IO_BACKEND_POLL
#endif

#include "shim/accept.h"
#include "shim/openat.h"
#include "shim/writev.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/coroutine.h>
#include <sc/forward.h>
#include <sc/io.h>

#include "../io.h"
#include "backend.h"
#include "poll_.h"

void sc_io_backend_init(ScIoBackend* backend) {
    assert(backend);

    backend->fd_count   = 512;
    backend->fds_active = 0;
    A3_UNWRAPN(backend->poll_fds, calloc(backend->fd_count, sizeof(*backend->poll_fds)));
    // NOLINTNEXTLINE(bugprone-sizeof-expression)
    A3_UNWRAPN(backend->coroutines, calloc(backend->fd_count, sizeof(*backend->coroutines)));

    for (size_t i = 0; i < backend->fd_count; i++)
        backend->poll_fds[i].fd = -1;
}

void sc_io_backend_destroy(ScIoBackend* backend) {
    assert(backend);

    free(backend->poll_fds);
    free(backend->coroutines);
}

static uint64_t timespec_to_ms(struct timespec t) {
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 100000;
}

void sc_io_backend_pump(ScIoBackend* backend, struct timespec deadline) {
    assert(backend);

    uint64_t deadline_ms = timespec_to_ms(deadline);

    struct timespec now;
    A3_UNWRAPSD(clock_gettime(CLOCK_MONOTONIC, &now));
    uint64_t now_ms = timespec_to_ms(now);

    uint64_t timeout = (deadline_ms > now_ms) ? deadline_ms - now_ms : 0;
    assert(timeout <= INT_MAX);

    A3_TRACE("Waiting for events.");
    int rc = poll(backend->poll_fds, backend->fd_count, (int)timeout);
    if ((rc < 0 && errno == EINTR) || !rc)
        return;
    if (rc < 0) {
        A3_ERRNO(errno, "poll");
        A3_PANIC("poll");
    }

    for (size_t count = 0, i = 0; count < (size_t)rc && i < backend->fd_count; i++) {
        short revents = backend->poll_fds[i].revents;
        if (!revents)
            continue;

        A3_TRACE("Handling event.");
        assert(backend->coroutines[i]);
        sc_co_resume(backend->coroutines[i], revents);
        count++;
    }
}

static size_t sc_io_poll_slot(void) {
    ScIoBackend* backend = &sc_co_event_loop()->backend;

    size_t ret = 0;
    if (backend->fds_active >= backend->fd_count) {
        ret = backend->fd_count;
        backend->fd_count *= 2;
        A3_UNWRAPN(backend->poll_fds,
                   realloc(backend->poll_fds, backend->fd_count * sizeof(*backend->poll_fds)));
        A3_UNWRAPN(backend->coroutines, // NOLINTNEXTLINE(bugprone-sizeof-expression)
                   realloc(backend->coroutines, backend->fd_count * sizeof(*backend->coroutines)));
    }

    for (; ret < backend->fd_count; ret++) {
        if (!backend->coroutines[ret])
            return ret;
    }

    A3_UNREACHABLE();
}

static SC_IO_RESULT(bool) sc_io_wait(ScFd fd, short events) {
    assert(events);

    ScIoBackend* backend = &sc_co_event_loop()->backend;

    size_t i                    = sc_io_poll_slot();
    backend->poll_fds[i].fd     = fd;
    backend->poll_fds[i].events = events;
    backend->coroutines[i]      = sc_co_current();

    while (!(sc_co_yield() & (events | POLLHUP | POLLERR | POLLNVAL))) {}

    backend->poll_fds[i].events = 0;
    backend->poll_fds[i].fd     = -1;
    backend->coroutines[i]      = NULL;

    short revents = backend->poll_fds[i].revents;
    if (revents & (POLLERR | POLLHUP | POLLNVAL))
        return SC_IO_ERR(bool, SC_IO_EOF);

    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(ScFd)
sc_io_accept(ScFd sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    assert(sock >= 0);
    assert(client_addr);
    assert(addr_len && *addr_len);

    while (true) {
        ScFd ret = sc_shim_accept(sock, client_addr, addr_len, SC_SOCK_NONBLOCK);

        if (ret >= 0)
            return SC_IO_OK(ScFd, ret);

        switch (errno) {
        case EINTR:
            continue;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(ScFd, sc_io_wait(sock, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "accept");
            A3_PANIC("accept failed");
        }
    }
}

SC_IO_RESULT(ScFd) sc_io_open_under(ScFd dir, A3CString path, uint64_t flags) {
    assert(dir >= 0 || dir == AT_FDCWD);
    assert(path.ptr);

    ScFd res = sc_shim_openat(dir, a3_string_cstr(path), flags | O_NONBLOCK, SC_RESOLVE_BENEATH);

    if (res < 0) {
        if (errno == EACCES || errno == ENOENT || errno == ELOOP)
            return SC_IO_ERR(ScFd, SC_IO_FILE_NOT_FOUND);

        A3_ERRNO_F(errno, "open of \"" A3_S_F "\" failed", A3_S_FORMAT(path));
        A3_PANIC("open failed");
    }

    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(bool) sc_io_close(ScFd file) {
    assert(file >= 0);

    A3_UNWRAPSD(close(file));

    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(size_t) sc_io_recv(ScFd sock, A3String dst) {
    assert(sock >= 0);
    assert(dst.ptr);

    while (true) {
        ssize_t res = recv(sock, dst.ptr, dst.len, 0);

        if (res > 0)
            return SC_IO_OK(size_t, (size_t)res);
        if (!res)
            return SC_IO_ERR(size_t, SC_IO_EOF);

        switch (errno) {
        case EINTR:
            continue;
        case ECONNRESET:
            return SC_IO_ERR(size_t, SC_IO_EOF);
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(size_t, sc_io_wait(sock, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "recv");
            A3_PANIC("recv failed");
        }
    }
}

SC_IO_RESULT(size_t)
sc_io_read_raw(ScFd fd, A3String dst, size_t count, off_t offset) {
    assert(fd >= 0);
    assert(dst.ptr);

    size_t to_read = MIN(count, dst.len);

    while (true) {
        ssize_t res = pread(fd, dst.ptr, to_read, offset);
        if (res > 0)
            return SC_IO_OK(size_t, (size_t)res);
        if (!res)
            return SC_IO_ERR(size_t, SC_IO_EOF);

        switch (errno) {
        case EINTR:
            break;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(size_t, sc_io_wait(fd, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "read");
            A3_PANIC("read failed");
        }
    }
}

SC_IO_RESULT(size_t)
sc_io_writev_raw(ScFd fd, struct iovec const* iov, unsigned count) {
    assert(fd >= 0);
    assert(iov);
    assert(count);
    assert(count <= INT_MAX);

    while (true) {
        ssize_t res = sc_shim_writev(fd, iov, (int)count, -1);
        if (res > 0)
            return SC_IO_OK(size_t, (size_t)res);
        if (!res)
            return SC_IO_ERR(size_t, SC_IO_EOF);

        if (res < 0) {
            switch (errno) {
            case EINTR:
                break;
            case EAGAIN:
#if EWOULDBLOCK != EAGAIN
            case EWOULDBLOCK:
#endif
                SC_IO_TRY(size_t, sc_io_wait(fd, POLLOUT));
                break;
            default:
                A3_ERRNO(errno, "writev");
                A3_PANIC("writev failed");
            }
        }
    }
}

SC_IO_RESULT(bool) sc_io_stat(ScFd file, struct stat* statbuf) {
    assert(file >= 0);
    assert(statbuf);

    if (fstat(file, statbuf) < 0) {
        switch (errno) {
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(bool, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO(errno, "fstat");
        A3_PANIC("fstat failed.");
    }

    return SC_IO_OK(bool, true);
}
