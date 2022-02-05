/*
 * SHORT CIRCUIT: EVENT LOOP -- Event submission.
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

#include "loop.hh"

#include <charconv>
#include <memory>
#include <string_view>

#include <liburing.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/util.h>

#include "config.hh"
#include "coro.hh"

namespace sc::ev {

// Check that the kernel is recent enough to support io_uring and io_uring_probe.
static void kver_check() {
    struct utsname info;
    A3_UNWRAPSD(uname(&info));

    std::string_view release { info.release };

    auto   dot           = release.find(".");
    size_t version_major = 0;
    size_t version_minor = 0;
    std::from_chars(release.data(), release.data() + dot, version_major);
    std::from_chars(release.data() + dot, &*release.end(), version_minor);

    if (version_major < config::MIN_KERNEL_VERSION_MAJOR ||
        (version_major == config::MIN_KERNEL_VERSION_MAJOR &&
         version_minor < config::MIN_KERNEL_VERSION_MINOR))
        A3_PANIC_FMT("Kernel version %s is not supported. At least %d.%d is required.", release,
                     config::MIN_KERNEL_VERSION_MAJOR, config::MIN_KERNEL_VERSION_MINOR);
}

#define REQUIRE_OP(P, OP)                                                                          \
    do {                                                                                           \
        if (!io_uring_opcode_supported(P.get(), OP))                                               \
            A3_PANIC_FMT("Required io_uring op %s is not supported by the kernel.", #OP);          \
    } while (0)

// All ops used should be checked here.
static void ops_check() {
    std::unique_ptr<io_uring_probe> probe { io_uring_get_probe() };

    REQUIRE_OP(probe, IORING_OP_ACCEPT);
    REQUIRE_OP(probe, IORING_OP_ASYNC_CANCEL);
    REQUIRE_OP(probe, IORING_OP_CLOSE);
    REQUIRE_OP(probe, IORING_OP_READ);
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_SEND);
    REQUIRE_OP(probe, IORING_OP_SPLICE);
    REQUIRE_OP(probe, IORING_OP_TIMEOUT);
}

#undef REQUIRE_OP

// Set the given resource to its hard limit and return the new state.
static rlimit rlimit_maximize(int resource) {
    rlimit lim;

    A3_UNWRAPSD(getrlimit(resource, &lim));
    lim.rlim_cur = lim.rlim_max;
    A3_UNWRAPSD(setrlimit(resource, &lim));
    return lim;
}

// Check and set resource limits.
static void limits_init(void) {
    rlimit lim_memlock = rlimit_maximize(RLIMIT_MEMLOCK);
    // This is a crude check, but opening the queue will almost certainly fail
    // if the limit is this low.
    if (lim_memlock.rlim_cur <= 96 * config::URING_ENTRIES)
        a3_log_fmt(LOG_WARN,
                   "The memlock limit (%d) is too low. The queue will probably "
                   "fail to open. Either raise the limit or lower `URING_ENTRIES`.",
                   lim_memlock.rlim_cur);

    struct rlimit lim_nofile = rlimit_maximize(RLIMIT_NOFILE);
    if (lim_nofile.rlim_cur <= config::CONNECTION_POOL_SIZE * 3)
        a3_log_fmt(LOG_WARN,
                   "The open file limit (%d) is low. Large numbers of concurrent "
                   "connections will probably cause \"too many open files\" errors.",
                   lim_nofile.rlim_cur);
}

EventLoop::EventLoop() {
    kver_check();
    ops_check();
    limits_init();

    bool opened = false;
    for (uint32_t queue_size = config::URING_ENTRIES; queue_size >= 512; queue_size /= 2) {
        if (!io_uring_queue_init(queue_size, &m_uring, 0)) {
            opened = true;
            break;
        }
    }
    if (!opened)
        A3_PANIC("Unable to open queue. The memlock limit is probably too low.");
}

EventLoop::~EventLoop() { io_uring_queue_exit(&m_uring); }

} // namespace sc::ev
