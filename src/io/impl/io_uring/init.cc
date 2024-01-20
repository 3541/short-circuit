/*
 * URING INIT -- io_uring initialization
 *
 * Copyright (c) 2020-2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <utility>

#include <liburing.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include "a3/log.h"
#include "a3/util.h"

module sc.io.impl.uring.init;

import sc.config;
import sc.lib.error;

namespace sc::io::impl::uring {

namespace {

using Version = std::pair<std::size_t, std::size_t>;

constexpr std::size_t ENTRY_SIZE{96};

Version kver() {
    utsname info{};
    lib::error::must(uname(&info));

    std::size_t major = 0;
    std::size_t minor = 0;

    lib::error::must(std::sscanf(info.release, "%zu.%zu.", &major, &minor), 2);

    return {major, minor};
}

void ops(Version version) {
    std::unique_ptr<::io_uring_probe, decltype(&::io_uring_free_probe)> probe{
        ::io_uring_get_probe(), ::io_uring_free_probe};

    if (!probe)
        A3_PANIC_FMT("Failed to get io_uring probe. Kernel (%zu.%zu) is probably too old.",
                     version.first, version.second);

#define REQUIRED(OP)                                                                               \
    do {                                                                                           \
        if (!::io_uring_opcode_supported(&*probe, (OP)))                                           \
            A3_PANIC("Kernel does not support required operation: " #OP);                          \
    } while (false)

    REQUIRED(IORING_OP_NOP);
    REQUIRED(IORING_OP_ACCEPT);
    REQUIRED(IORING_OP_RECV);
    REQUIRED(IORING_OP_CLOSE);

#undef REQUIRED
}

void limits(unsigned entries) {
    auto maximize = [](int res) {
        rlimit ret{};

        lib::error::must(getrlimit(res, &ret));
        ret.rlim_cur = ret.rlim_max;
        lib::error::must(setrlimit(res, &ret));

        return ret;
    };

    auto const memlock = maximize(RLIMIT_MEMLOCK);
    if (memlock.rlim_cur <= entries * ENTRY_SIZE) {
        A3_WARN_F("The memlock limit (%zu) is too small. The ring will probably fail to open. "
                  "Either raise the limit or construct the ring with fewer entries.",
                  memlock.rlim_cur);
    }

    maximize(RLIMIT_NOFILE);
    // TODO: Heuristic to check open file limit.
}

unsigned supported_flags(Version version) {
    unsigned flags = 0;

    if (version >= Version{5, 18})
        flags |= IORING_SETUP_SUBMIT_ALL;
    if (version >= Version{5, 19})
        flags |= IORING_SETUP_COOP_TASKRUN;
    if (version >= Version{6, 0})
        flags |= IORING_SETUP_SINGLE_ISSUER;
    if (version >= Version{6, 1})
        flags |= IORING_SETUP_DEFER_TASKRUN;

    return flags;
}

void features_check(std::uint32_t features) {
#define REQUIRED(F)                                                                                \
    do {                                                                                           \
        if (!(features & (F)))                                                                     \
            A3_PANIC("Kernel does not support required feature: " #F);                             \
    } while (false)

    REQUIRED(IORING_FEAT_SUBMIT_STABLE);
    REQUIRED(IORING_FEAT_CQE_SKIP);
    REQUIRED(IORING_FEAT_LINKED_FILE);

#undef REQUIRED
}

} // namespace

void init(::io_uring& uring, unsigned entries) {
    auto version = kver();
    ops(version);
    limits(entries);
    auto const flags = supported_flags(version);

    ::io_uring_params params{.flags = flags};

    lib::error::must(::io_uring_queue_init_params(entries, &uring, &params));
    features_check(params.features);

    lib::error::must(::io_uring_register_ring_fd(&uring));
    lib::error::must(::io_uring_register_files_sparse(&uring, config::FILE_LIMIT));
}

} // namespace sc::io::impl::uring
