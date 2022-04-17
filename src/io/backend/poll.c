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

#ifdef __linux__
#define _GNU_SOURCE // For accept4 and statx.
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/version.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/coroutine.h>
#include <sc/forward.h>
#include <sc/io.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
#include <linux/openat2.h>
#endif

typedef struct ScEventLoop {
    struct pollfd* poll_fds;
    ScCoroutine**  coroutines;
    size_t         fd_count;
    size_t         fds_active;
} ScEventLoop;

ScEventLoop* sc_io_event_loop_new() {
    A3_TRACE("Creating event loop.");

    A3_UNWRAPNI(ScEventLoop*, ret, calloc(1, sizeof(*ret)));

    ret->fd_count   = 512;
    ret->fds_active = 0;
    A3_UNWRAPN(ret->poll_fds, calloc(ret->fd_count, sizeof(*ret->poll_fds)));
    A3_UNWRAPN(ret->coroutines, calloc(ret->fd_count, sizeof(*ret->coroutines)));

    for (size_t i = 0; i < ret->fd_count; i++)
        ret->poll_fds[i].fd = -1;

    return ret;
}

void sc_io_event_loop_free(ScEventLoop* ev) {
    assert(ev);

    free(ev->poll_fds);
    free(ev->coroutines);
    free(ev);
}

void sc_io_event_loop_pump(ScEventLoop* ev) {
    assert(ev);

    A3_TRACE("Waiting for events.");
    int rc = poll(ev->poll_fds, ev->fd_count, -1);
    if ((rc < 0 && errno == EINTR) || !rc)
        return;
    if (rc < 0) {
        A3_ERRNO(errno, "poll");
        A3_PANIC("poll");
    }

    for (size_t count = 0, i = 0; count < (size_t)rc && i < ev->fd_count; i++) {
        short revents = ev->poll_fds[i].revents;
        if (!revents)
            continue;

        A3_TRACE("Handling event.");
        assert(ev->coroutines[i]);
        sc_co_resume(ev->coroutines[i], revents);
        count++;
    }
}

static size_t sc_io_poll_slot(ScCoroutine* self) {
    assert(self);

    ScEventLoop* ev = sc_co_event_loop(self);

    size_t ret = 0;
    if (ev->fds_active >= ev->fd_count) {
        ret = ev->fd_count;
        ev->fd_count *= 2;
        A3_UNWRAPN(ev->poll_fds, realloc(ev->poll_fds, ev->fd_count * sizeof(*ev->poll_fds)));
        A3_UNWRAPN(ev->coroutines, realloc(ev->coroutines, ev->fd_count * sizeof(*ev->coroutines)));
    }

    for (; ret < ev->fd_count; ret++) {
        if (!ev->coroutines[ret])
            return ret;
    }

    A3_UNREACHABLE();
}

static SC_IO_RESULT(bool) sc_io_wait(ScCoroutine* self, ScFd fd, short events) {
    assert(self);
    assert(events);

    ScEventLoop* ev = sc_co_event_loop(self);

    size_t i               = sc_io_poll_slot(self);
    ev->poll_fds[i].fd     = fd;
    ev->poll_fds[i].events = events;
    ev->coroutines[i]      = self;

    while (!(sc_co_yield(self) & (events | POLLHUP | POLLERR | POLLNVAL)))
        ;

    ev->poll_fds[i].events = 0;
    ev->poll_fds[i].fd     = -1;
    ev->coroutines[i]      = NULL;

    short revents = ev->poll_fds[i].revents;
    if (revents & (POLLERR | POLLHUP | POLLNVAL))
        return SC_IO_ERR(bool, SC_IO_SOCKET_CLOSED);

    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(ScFd)
sc_io_accept(ScCoroutine* self, ScFd sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    assert(self);
    assert(sock >= 0);
    assert(client_addr);
    assert(addr_len && *addr_len);

    while (true) {
#ifdef __linux__
        ScFd ret = accept4(sock, client_addr, addr_len, SOCK_NONBLOCK);
#endif
        if (ret >= 0)
            return SC_IO_OK(ScFd, ret);

        switch (errno) {
        case EINTR:
            continue;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(ScFd, sc_io_wait(self, sock, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "accept");
            A3_PANIC("accept failed");
        }
    }
}

SC_IO_RESULT(ScFd) sc_io_open_under(ScCoroutine* self, ScFd dir, A3CString path, uint64_t flags) {
    assert(self);
    assert(dir >= 0 || dir == AT_FDCWD);
    assert(path.ptr);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
    ScFd res = (ScFd)syscall(
        SYS_openat2, dir, a3_string_cstr(path),
        &(struct open_how) { .flags = flags | O_NONBLOCK, .resolve = RESOLVE_BENEATH },
        sizeof(struct open_how));
#else
    ScFd res = openat(dir, a3_string_cstr(path), (int)flags | O_NONBLOCK | O_NOFOLLOW);
    // TODO: Validate path is child of dir.
#endif

    if (res < 0) {
        if (errno == EACCES || errno == ENOENT || errno == ELOOP)
            return SC_IO_ERR(ScFd, SC_IO_FILE_NOT_FOUND);

        A3_ERRNO_F(errno, "open of \"" A3_S_F "\" failed", A3_S_FORMAT(path));
        A3_PANIC("open failed");
    }

    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(bool) sc_io_close(ScCoroutine* self, ScFd file) {
    assert(self);
    assert(file >= 0);

    A3_UNWRAPSD(close(file));

    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(size_t) sc_io_recv(ScCoroutine* self, ScFd sock, A3String dst) {
    assert(self);
    assert(sock >= 0);
    assert(dst.ptr);

    while (true) {
        ssize_t res = recv(sock, dst.ptr, dst.len, 0);

        if (res > 0)
            return SC_IO_OK(size_t, (size_t)res);
        if (!res)
            return SC_IO_ERR(size_t, SC_IO_SOCKET_CLOSED);

        switch (errno) {
        case EINTR:
            continue;
        case ECONNRESET:
            return SC_IO_ERR(size_t, SC_IO_SOCKET_CLOSED);
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(size_t, sc_io_wait(self, sock, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "recv");
            A3_PANIC("recv failed");
        }
    }
}

SC_IO_RESULT(size_t)
sc_io_read(ScCoroutine* self, ScFd fd, A3String dst, size_t count, off_t offset) {
    assert(self);
    assert(fd >= 0);
    assert(dst.ptr);

    size_t to_read = MIN(count, dst.len);
    size_t total   = 0;

    while (true) {
        ssize_t res = pread(fd, dst.ptr, dst.len, offset);

        if (res >= 0) {
            total += (size_t)res;

            if (total >= to_read)
                return SC_IO_OK(size_t, (size_t)total);
        }

        switch (errno) {
        case EINTR:
            continue;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(size_t, sc_io_wait(self, fd, POLLIN));
            break;
        default:
            A3_ERRNO(errno, "read");
            A3_PANIC("read failed");
        }
    }
}

SC_IO_RESULT(size_t)
sc_io_writev(ScCoroutine* self, ScFd fd, struct iovec const* iov, unsigned count) {
    assert(self);
    assert(fd >= 0);
    assert(iov);
    assert(count > 0);
    assert(count <= INT_MAX);

    while (true) {
        ssize_t res = pwritev2(fd, iov, (int)count, -1, 0);

        if (res >= 0)
            return SC_IO_OK(size_t, (size_t)res);

        switch (errno) {
        case EINTR:
            continue;
        case EAGAIN:
#if EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            SC_IO_TRY(size_t, sc_io_wait(self, fd, POLLOUT));
            break;
        default:
            A3_ERRNO(errno, "writev");
            A3_PANIC("writev failed");
        }
    }
}

SC_IO_RESULT(bool) sc_io_stat(ScCoroutine* self, ScFd file, struct statx* statbuf, unsigned mask) {
    assert(self);
    assert(file >= 0);
    assert(statbuf);

    if (statx(file, "", AT_EMPTY_PATH, mask, statbuf) < 0) {
        switch (errno) {
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(bool, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO(errno, "statx");
        A3_PANIC("statx failed.");
    }

    return SC_IO_OK(bool, true);
}
