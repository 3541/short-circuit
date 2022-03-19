
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
#include <liburing.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/log.h>
#include <a3/util.h>

#include <sc/coroutine.h>
#include <sc/io.h>

#include "config.h"
#include "sc/forward.h"

typedef struct ScEventLoop {
    struct io_uring uring;
} ScEventLoop;

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
    if (lim_memlock.rlim_cur <= 96 * SC_URING_ENTRIES)
        A3_WARN_F("The memlock limit (%d) is too low. The queue will probably "
                  "fail to open. Either raise the limit or lower `URING_ENTRIES`.",
                  lim_memlock.rlim_cur);

    struct rlimit lim_nofile = sc_io_limit_maximize(RLIMIT_NOFILE);
    if (lim_nofile.rlim_cur <= SC_CONNECTION_POOL_SIZE * 3)
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

    REQUIRE_OP(probe, IORING_OP_READ);

#undef REQUIRE_OP

    io_uring_free_probe(probe);
}

ScEventLoop* sc_io_event_loop_new() {
    sc_io_ops_check();
    sc_io_limits_init();

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

void sc_io_event_loop_pump(ScEventLoop* ev) {
    assert(ev);

    if (!io_uring_submit_and_wait(&ev->uring, 1))
        return;

    struct io_uring_cqe* cqe;
    size_t               head;
    unsigned             count = 0;
    io_uring_for_each_cqe(&ev->uring, head, cqe) {
        count++;
        ScCoroutine* co = io_uring_cqe_get_data(cqe);
        if (!co) {
            A3_WARN("Empty CQE.");
            continue;
        }

        sc_co_resume(co, cqe->res);
    }

    io_uring_cq_advance(&ev->uring, count);
}

void sc_io_event_loop_free(ScEventLoop* ev) {
    assert(ev);

    io_uring_queue_exit(&ev->uring);
    free(ev);
}

ssize_t sc_co_await(ScCoroutine* self, ScIoFuture* future) {
    assert(self);
    assert(future);

    struct io_uring_sqe* sqe = (struct io_uring_sqe*)future;
    io_uring_sqe_set_data(sqe, self);

    return sc_co_yield(self);
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it is full. This /can/
// return a null pointer if the SQ is full and, for whatever reason, it does not empty in time.
static struct io_uring_sqe* sc_io_sqe_get(struct io_uring* uring) {
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

ScIoFuture* sc_io_openat(ScEventLoop* ev, ScCoroutine* co, ScFd dir, A3CString path, int flags) {
    assert(ev);
    assert(co);
    assert(path.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get(&ev->uring);
    A3_TRYB(sqe);

    io_uring_prep_openat(sqe, dir, a3_string_cstr(path), flags, 0);

    return (ScIoFuture*)sqe;
}

ScIoFuture* sc_io_read(ScEventLoop* ev, ScCoroutine* co, ScFd fd, A3String dst, size_t size,
                       off_t offset) {
    assert(ev);
    assert(co);
    assert(fd >= 0);
    assert(dst.ptr);

    struct io_uring_sqe* sqe = sc_io_sqe_get(&ev->uring);
    A3_TRYB(sqe);

    unsigned read_size = (unsigned)MIN(dst.len, size);
    io_uring_prep_read(sqe, fd, dst.ptr, read_size, (uint64_t)offset);

    return (ScIoFuture*)sqe;
}
