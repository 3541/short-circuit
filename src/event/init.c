/*
 * SHORT CIRCUIT: EVENT INIT -- Event system initialization.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
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

#include <liburing.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/log.h>

#include "config.h"
#include "event.h"
#include "internal.h"

// Check that the kernel is recent enough to support io_uring and
// io_uring_probe.
static void event_check_kver(void) {
    struct utsname info;
    A3_UNWRAPSD(uname(&info));

    char* release = strdup(info.release);

    long version_major = strtol(strtok(info.release, "."), NULL, 10);
    long version_minor = strtol(strtok(NULL, "."), NULL, 10);

    if (version_major < MIN_KERNEL_VERSION_MAJOR ||
        (version_major == MIN_KERNEL_VERSION_MAJOR && version_minor < MIN_KERNEL_VERSION_MINOR))
        A3_PANIC_FMT("Kernel version %s is not supported. At least %d.%d is required.", release,
                     MIN_KERNEL_VERSION_MAJOR, MIN_KERNEL_VERSION_MINOR);

    free(release);
}

#define REQUIRE_OP(P, OP)                                                                          \
    do {                                                                                           \
        if (!io_uring_opcode_supported(P, OP))                                                     \
            A3_PANIC_FMT("Required io_uring op %s is not supported by the kernel.", #OP);          \
    } while (0)

// All ops used should be checked here.
static void event_check_ops(struct io_uring* uring) {
    struct io_uring_probe* probe = io_uring_get_probe_ring(uring);

    REQUIRE_OP(probe, IORING_OP_ACCEPT);
    REQUIRE_OP(probe, IORING_OP_ASYNC_CANCEL);
    REQUIRE_OP(probe, IORING_OP_CLOSE);
    REQUIRE_OP(probe, IORING_OP_READ);
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_SEND);
    REQUIRE_OP(probe, IORING_OP_SPLICE);
    REQUIRE_OP(probe, IORING_OP_TIMEOUT);

    free(probe);
}

// Set the given resource to its hard limit and return the new state.
static struct rlimit rlimit_maximize(int resource) {
    struct rlimit lim;

    A3_UNWRAPSD(getrlimit(resource, &lim));
    lim.rlim_cur = lim.rlim_max;
    A3_UNWRAPSD(setrlimit(resource, &lim));
    return lim;
}

// Check and set resource limits.
static void event_limits_init(void) {
    struct rlimit lim_memlock = rlimit_maximize(RLIMIT_MEMLOCK);
    // This is a crude check, but opening the queue will almost certainly fail
    // if the limit is this low.
    if (lim_memlock.rlim_cur <= 96 * URING_ENTRIES)
        A3_WARN_F("The memlock limit (%d) is too low. The queue will probably "
                  "fail to open. Either raise the limit or lower `URING_ENTRIES`.",
                  lim_memlock.rlim_cur);

    struct rlimit lim_nofile = rlimit_maximize(RLIMIT_NOFILE);
    if (lim_nofile.rlim_cur <= CONNECTION_POOL_SIZE * 3)
        A3_WARN_F("The open file limit (%d) is low. Large numbers of concurrent "
                  "connections will probably cause \"too many open files\" errors.",
                  lim_nofile.rlim_cur);
}

struct io_uring event_init() {
    event_check_kver();
    event_limits_init();

    struct io_uring ret;

    bool opened = false;
    for (size_t queue_size = URING_ENTRIES; queue_size >= 512; queue_size /= 2) {
        if (!io_uring_queue_init(URING_ENTRIES, &ret, 0)) {
            opened = true;
            break;
        }
    }
    if (!opened)
        A3_PANIC("Unable to open queue. The memlock limit is probably too low.");

    event_check_ops(&ret);
    EVENT_POOL = A3_POOL_OF(Event, EVENT_POOL_SIZE, A3_POOL_ZERO_BLOCKS, NULL, NULL);

    return ret;
}
