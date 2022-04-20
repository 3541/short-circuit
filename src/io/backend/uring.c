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

// The below defines fix editor highlighting when configured for another backend. This doesn't break
// compilation, since this file is only ever built by inclusion from backend.c.
#ifndef SC_IO_BACKEND_URING
#undef SC_IO_BACKEND_POLL
#define SC_IO_BACKEND_URING
#endif

#include "uring.h"

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

#include "../io.h"
#include "backend.h"
#include "config.h"

// Defined here because including openat2.h duplicates struct open_how, which is also defined by
// liburing.
#define RESOLVE_BENEATH 0x08

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

void sc_io_backend_init(ScIoBackend* backend) {
    assert(backend);

#ifndef SC_TEST
    sc_io_ops_check();
    sc_io_limits_init();
#endif

    // Try to open the queue, with gradually decreasing queue sizes.
    bool opened = false;
    for (unsigned queue_size = SC_URING_ENTRIES; queue_size >= 512; queue_size /= 2) {
        if (!io_uring_queue_init(queue_size, &backend->uring, 0)) {
            opened = true;
            break;
        }
    }
    if (!opened)
        A3_PANIC("Unable to open queue. The memlock limit is probably too low.");
}

void sc_io_backend_destroy(ScIoBackend* backend) {
    assert(backend);

    io_uring_queue_exit(&backend->uring);
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it is full. This /can/
// return a null pointer if the SQ is full and, for whatever reason, it does not empty in time.
static struct io_uring_sqe* sc_io_sqe_get_from(ScIoBackend* backend) {
    assert(backend);

    struct io_uring* uring = &backend->uring;

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

static struct io_uring_sqe* sc_io_sqe_get(void) {
    return sc_io_sqe_get_from(&sc_co_event_loop()->backend);
}

void sc_io_backend_pump(ScIoBackend* backend, struct timespec deadline) {
    assert(backend);

    struct io_uring* uring = &backend->uring;

    struct io_uring_sqe* sqe = sc_io_sqe_get_from(backend);
    if A3_LIKELY (sqe) {
        io_uring_prep_timeout(
            sqe,
            &(struct __kernel_timespec) { .tv_sec = deadline.tv_sec, .tv_nsec = deadline.tv_nsec },
            1, IORING_TIMEOUT_ABS);
        sqe->user_data = LIBURING_UDATA_TIMEOUT;
    }

    A3_TRACE("Waiting for events.");
    io_uring_submit_and_wait(uring, 1);

    struct io_uring_cqe* cqe;
    size_t               head;
    unsigned             count = 0;
    io_uring_for_each_cqe(uring, head, cqe) {
        count++;

        if (cqe->user_data == LIBURING_UDATA_TIMEOUT)
            continue;

        A3_TRACE("Handling event.");
        ScCoroutine* co = io_uring_cqe_get_data(cqe);
        if (!co) {
            A3_WARN("Empty CQE.");
            continue;
        }

        sc_co_resume(co, cqe->res);
    }

    io_uring_cq_advance(uring, count);
}

static ssize_t sc_io_submit(struct io_uring_sqe* sqe) {
    assert(sqe);

    io_uring_sqe_set_data(sqe, sc_co_current());
    return sc_co_yield();
}

SC_IO_RESULT(ScFd)
sc_io_accept(ScFd sock, struct sockaddr* client_addr, socklen_t* addr_len) {
    assert(sock >= 0);
    assert(client_addr);
    assert(addr_len && *addr_len);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(ScFd, SC_IO_SUBMIT_FAILED));

    io_uring_prep_accept(sqe, sock, client_addr, addr_len, 0);

    ScFd res = -1;
    A3_UNWRAPS(res, (ScFd)sc_io_submit(sqe));
    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(ScFd) sc_io_open_under(ScFd dir, A3CString path, uint64_t flags) {
    assert(dir >= 0 || dir == AT_FDCWD);
    assert(path.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(ScFd, SC_IO_SUBMIT_FAILED));

    io_uring_prep_openat2(sqe, dir, a3_string_cstr(path),
                          &(struct open_how) { .flags = flags, .resolve = RESOLVE_BENEATH });

    ScFd res = -1;
    if ((res = (ScFd)sc_io_submit(sqe)) < 0) {
        switch (-res) {
        case EAGAIN:
            return sc_io_open_under(dir, path, flags);
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(ScFd, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO_F(-res, "open of \"" A3_S_F "\" failed", A3_S_FORMAT(path));
        A3_PANIC("open failed");
    }

    return SC_IO_OK(ScFd, res);
}

SC_IO_RESULT(bool) sc_io_close(ScFd file) {
    assert(file >= 0);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(bool, SC_IO_SUBMIT_FAILED));

    io_uring_prep_close(sqe, file);

    A3_UNWRAPSD(sc_io_submit(sqe));
    return SC_IO_OK(bool, true);
}

SC_IO_RESULT(size_t) sc_io_recv(ScFd sock, A3String dst) {
    assert(sock >= 0);
    assert(dst.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

    io_uring_prep_recv(sqe, sock, dst.ptr, dst.len, 0);

    ssize_t res = sc_io_submit(sqe);
    if (res <= 0) {
        switch (-res) {
        case 0:
        case ECONNRESET:
            return SC_IO_ERR(size_t, SC_IO_EOF);
        }
        A3_ERRNO(-(int)res, "recv");
        A3_PANIC("recv failed");
    }
    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(size_t)
sc_io_read_raw(ScFd fd, A3String dst, size_t count, off_t offset) {
    assert(fd >= 0);
    assert(dst.ptr);
    assert(dst.len <= UINT_MAX);

    size_t to_read = MIN(count, dst.len);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
    io_uring_prep_read(sqe, fd, dst.ptr, (unsigned int)to_read, (uint64_t)offset);
#else
    struct iovec vec[] = { { .iov_base = dst.ptr, .iov_len = to_read } };
    io_uring_prep_readv(sqe, fd, vec, 1, (uint64_t)offset);
#endif

    ssize_t res = -1;
    A3_UNWRAPS(res, sc_io_submit(sqe));

    if (!res)
        return SC_IO_ERR(size_t, SC_IO_EOF);
    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(size_t)
sc_io_writev_raw(ScFd fd, struct iovec const* iov, unsigned count) {
    assert(fd >= 0);
    assert(iov);
    assert(count > 0);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(size_t, SC_IO_SUBMIT_FAILED));

    io_uring_prep_writev(sqe, fd, iov, count, 0);

    ssize_t res = -1;
    A3_UNWRAPS(res, sc_io_submit(sqe));
    if (!res)
        return SC_IO_ERR(size_t, SC_IO_EOF);

    return SC_IO_OK(size_t, (size_t)res);
}

SC_IO_RESULT(bool) sc_io_stat(ScFd file, struct stat* statbuf) {
    assert(file >= 0);
    assert(statbuf);

    struct io_uring_sqe* sqe = sc_io_sqe_get();
    A3_TRYB_MAP(sqe, SC_IO_ERR(bool, SC_IO_SUBMIT_FAILED));

    struct statx statxbuf;

    io_uring_prep_statx(sqe, file, "", AT_EMPTY_PATH,
                        STATX_TYPE | STATX_SIZE | STATX_MTIME | STATX_INO, &statxbuf);

    ssize_t res = sc_io_submit(sqe);
    if (res < 0) {
        switch (-res) {
        case EACCES:
        case ENOENT:
            return SC_IO_ERR(bool, SC_IO_FILE_NOT_FOUND);
        }
        A3_ERRNO(-(int)res, "statx");
        A3_PANIC("statx failed.");
    }

    statbuf->st_mode         = statxbuf.stx_mode;
    statbuf->st_size         = (off_t)statxbuf.stx_size;
    statbuf->st_mtim.tv_sec  = statxbuf.stx_mtime.tv_sec;
    statbuf->st_mtim.tv_nsec = statxbuf.stx_mtime.tv_nsec;
    statbuf->st_ino          = statxbuf.stx_ino;

    return SC_IO_OK(bool, true);
}
