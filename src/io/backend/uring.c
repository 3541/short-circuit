/*
 * SHORT CIRCUIT: URING -- io_uring event backend.
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

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <limits.h>
#include <linux/version.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/coroutine.h>
#include <sc/forward.h>
#include <sc/io.h>

#include "config.h"

// Defined here because including openat2.h duplicates struct open_how, which is also defined by
// liburing.
#define RESOLVE_BENEATH 0x08

typedef struct ScEventLoop {
    struct io_uring uring;
} ScEventLoop;

#ifndef SC_TEST
// Check that the kernel is sufficiently recent to support io_uring and io_uring_probe, which will
// allow more specific feature checks.
static void sc_io_kver_check(void) {
    struct utsname info;
    A3_UNWRAPSD(uname(&info));

    char* release = strdup(info.release);
    char* saveptr = NULL;

    long version_major = strtol(strtok_r(info.release, ".", &saveptr), NULL, 10);
    long version_minor = strtol(strtok_r(NULL, ".", &saveptr), NULL, 10);

    if (version_major < SC_MIN_KERNEL_VERSION_MAJOR ||
        (version_major == SC_MIN_KERNEL_VERSION_MAJOR &&
         version_minor < SC_MIN_KERNEL_VERSION_MINOR))
        A3_PANIC_FMT("Kernel version %s is not supported. At least %d.%d is required.", release,
                     SC_MIN_KERNEL_VERSION_MAJOR, SC_MIN_KERNEL_VERSION_MINOR);

    free(release);
}

// Set the given resource to its hard limit and return the new state.
static struct rlimit sc_io_limit_maximize(int resource) {
    struct rlimit lim;

    A3_UNWRAPSD(getrlimit(resource, &lim));
    lim.rlim_cur = lim.rlim_max;
    A3_UNWRAPSD(setrlimit(resource, &lim));
    return lim;
}

// Check and set resource limits.
static void sc_io_limits_init(void) {
    struct rlimit lim_memlock = sc_io_limit_maximize(RLIMIT_MEMLOCK);
    // This is a crude check, but opening the queue will almost certainly fail
    // if the limit is this low.
    if (lim_memlock.rlim_cur <= 96ULL * SC_URING_ENTRIES)
        A3_WARN_F("The memlock limit (%d) is too low. The queue will probably "
                  "fail to open. Either raise the limit or lower `URING_ENTRIES`.",
                  lim_memlock.rlim_cur);

    struct rlimit lim_nofile = sc_io_limit_maximize(RLIMIT_NOFILE);
    if (lim_nofile.rlim_cur <= 3ULL * SC_CONNECTION_POOL_SIZE)
        A3_WARN_F("The open file limit (%d) is low. Large numbers of concurrent "
                  "connections will probably cause \"too many open files\" errors.",
                  lim_nofile.rlim_cur);
}

// Check for required io_uring operations.
static void sc_io_ops_check(void) {
    sc_io_kver_check();

    struct io_uring_probe* probe = io_uring_get_probe();

#define REQUIRE_OP(P, OP)                                                                          \
    do {                                                                                           \
        if (!io_uring_opcode_supported(P, OP))                                                     \
            A3_PANIC_FMT("Required io_uring op %s is not supported by the kernel.", #OP);          \
    } while (0)

    REQUIRE_OP(probe, IORING_OP_ACCEPT);
    REQUIRE_OP(probe, IORING_OP_OPENAT2);
    REQUIRE_OP(probe, IORING_OP_CLOSE);
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_READV);
    REQUIRE_OP(probe, IORING_OP_WRITEV);
    REQUIRE_OP(probe, IORING_OP_STATX);

#undef REQUIRE_OP

    io_uring_free_probe(probe);
}
#endif

ScEventLoop* sc_io_event_loop_new() {
    A3_TRACE("Creating event loop.");

#ifndef SC_TEST
    sc_io_ops_check();
    sc_io_limits_init();
#endif

    A3_UNWRAPNI(ScEventLoop*, ret, calloc(1, sizeof(*ret)));

    // Try to open the queue, with gradually decreasing queue sizes.
    bool opened = false;
    for (unsigned queue_size = SC_URING_ENTRIES; queue_size >= 512; queue_size /= 2) {
        if (!io_uring_queue_init(queue_size, &ret->uring, 0)) {
            opened = true;
            break;
        }
    }
    if (!opened)
        A3_PANIC("Unable to open queue. The memlock limit is probably too low.");

    return ret;
}

void sc_io_event_loop_free(ScEventLoop* ev) {
    assert(ev);

    io_uring_queue_exit(&ev->uring);
    free(ev);
}

void sc_io_event_loop_pump(ScEventLoop* ev) {
    assert(ev);

    A3_TRACE("Waiting for events.");
    io_uring_submit_and_wait(&ev->uring, 1);

    struct io_uring_cqe* cqe;
    size_t               head;
    unsigned             count = 0;
    io_uring_for_each_cqe(&ev->uring, head, cqe) {
        count++;

        A3_TRACE("Handling event.");
        ScCoroutine* co = io_uring_cqe_get_data(cqe);
        if (!co) {
            A3_WARN("Empty CQE.");
            continue;
        }

        sc_co_resume(co, cqe->res);
    }

    io_uring_cq_advance(&ev->uring, count);
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it is full. This /can/
// return a null pointer if the SQ is full and, for whatever reason, it does not empty in time.
static struct io_uring_sqe* sc_io_sqe_get(ScCoroutine* co) {
    assert(co);

    struct io_uring* uring = &sc_co_event_loop(co)->uring;

    struct io_uring_sqe* ret = io_uring_get_sqe(uring);
    // Try to submit events until an SQE is available or too many retries have elapsed.
    for (size_t retries = 0; !ret && retries < SC_URING_SQE_RETRY_MAX;
         ret            = io_uring_get_sqe(uring), retries++)
        if (io_uring_submit(uring) < 0)
            break;
    if (!ret)
        A3_WARN("SQ full.");
    return ret;
}

static ssize_t sc_io_submit(ScCoroutine* self, struct io_uring_sqe* sqe) {
    assert(self);
    assert(sqe);

    io_uring_sqe_set_data(sqe, self);
    return sc_co_yield(self);
}

SC_IO_RESULT(ScFd)
sc_io_accept(ScCoroutine* self, ScFd sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    assert(self);
    assert(sock >= 0);
    assert(client_addr);
    assert(addr_len && *addr_len);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(ScFd, SC_IO_SUBMIT_FAILED));

    io_uring_prep_accept(sqe, sock, client_addr, addr_len, 0);

    ScFd res = -1;
    A3_UNWRAPS(res, (ScFd)sc_io_submit(self, sqe));
    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(ScFd) sc_io_open_under(ScCoroutine* self, ScFd dir, A3CString path, uint64_t flags) {
    assert(self);
    assert(dir >= 0 || dir == AT_FDCWD);
    assert(path.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(ScFd, SC_IO_SUBMIT_FAILED));

    io_uring_prep_openat2(sqe, dir, a3_string_cstr(path),
                          &(struct open_how) { .flags = flags, .resolve = RESOLVE_BENEATH });

    ScFd res = -1;
    if ((res = (ScFd)sc_io_submit(self, sqe)) < 0) {
        switch (-res) {
        case EAGAIN:
            return sc_io_open_under(self, dir, path, flags);
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(ScFd, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO_F(-res, "open of \"" A3_S_F "\" failed", A3_S_FORMAT(path));
        A3_PANIC("open failed");
    }

    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(bool) sc_io_close(ScCoroutine* self, ScFd file) {
    assert(self);
    assert(file >= 0);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(bool, SC_IO_SUBMIT_FAILED));

    io_uring_prep_close(sqe, file);

    A3_UNWRAPSD(sc_io_submit(self, sqe));
    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(size_t) sc_io_recv(ScCoroutine* self, ScFd sock, A3String dst) {
    assert(self);
    assert(sock >= 0);
    assert(dst.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

    io_uring_prep_recv(sqe, sock, dst.ptr, dst.len, 0);

    ssize_t res = sc_io_submit(self, sqe);
    if (res <= 0) {
        switch (-res) {
        case 0:
        case ECONNRESET:
            return SC_IO_ERR(size_t, SC_IO_SOCKET_CLOSED);
        }
        A3_ERRNO(-(int)res, "recv");
        A3_PANIC("recv failed");
    }
    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(size_t)
sc_io_read(ScCoroutine* self, ScFd fd, A3String dst, size_t count, off_t offset) {
    assert(self);
    assert(fd >= 0);
    assert(dst.ptr);
    assert(dst.len <= UINT_MAX);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

    size_t to_read = MIN(count, dst.len);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
    io_uring_prep_read(sqe, fd, dst.ptr, (unsigned int)to_read, (uint64_t)offset);
#else
    struct iovec vec[] = { { .iov_base = dst.ptr, .iov_len = dst.len } };
    io_uring_prep_readv(sqe, fd, vec, 1, (uint64_t)offset);
#endif

    ssize_t res = -1;
    A3_UNWRAPS(res, sc_io_submit(self, sqe));
    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(size_t)
sc_io_writev(ScCoroutine* self, ScFd fd, struct iovec const* iov, unsigned count) {
    assert(self);
    assert(fd >= 0);
    assert(iov);
    assert(count > 0);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

    io_uring_prep_writev(sqe, fd, iov, count, 0);

    ssize_t res = -1;
    A3_UNWRAPS(res, sc_io_submit(self, sqe));
    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(bool) sc_io_stat(ScCoroutine* self, ScFd file, struct stat* statbuf) {
    assert(self);
    assert(file >= 0);
    assert(statbuf);

    struct io_uring_sqe* sqe = sc_io_sqe_get(self);
    A3_TRYB_MAP(sqe, SC_IO_ERR(bool, SC_IO_SUBMIT_FAILED));

    struct statx statxbuf;

    io_uring_prep_statx(sqe, file, "", AT_EMPTY_PATH, STATX_TYPE | STATX_SIZE, &statxbuf);

    ssize_t res = sc_io_submit(self, sqe);
    if (res < 0) {
        switch (-res) {
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(bool, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO(-(int)res, "statx");
        A3_PANIC("statx failed.");
    }

    statbuf->st_mode = statxbuf.stx_mode;
    statbuf->st_size = (off_t)statxbuf.stx_size;

    return SC_IO_OK(bool, true);
}
