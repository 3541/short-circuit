/*
 * SHORT CIRCUIT: URING — io_uring event loop.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <liburing.h>
#include <liburing/io_uring.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <sc/io/backend/uring/uring.hh>
#include <sc/io/net/connection_pool.hh>
#include <sc/lib/result.hh>
#include <sc/lib/try.hh>

#ifndef IORING_SETUP_SINGLE_ISSUER
#define IORING_SETUP_SINGLE_ISSUER (1U << 12)
#endif

namespace sc::io::backend::uring {

namespace {

using KernelVersion = std::pair<std::uint32_t, std::uint32_t>;

constexpr KernelVersion MIN_KERNEL_VERSION{5, 6};
constexpr std::size_t   ENTRY_SIZE                    = 96;
constexpr auto          EXPECTED_FILES_PER_CONNECTION = 3;

lib::Result<KernelVersion, std::error_code> kernel_version() {
    utsname info;
    SC_TRY_ERRNO(uname(&info));

    std::string_view release{info.release};
    auto             split = release.find('.');

    auto version_major = std::stoul(std::string{release.substr(0, split)});
    auto version_minor = std::stoul(std::string{release.substr(split)});

    return lib::Ok{KernelVersion{version_major, version_minor}};
}

auto uring_probe() {
    return std::unique_ptr<io_uring_probe, decltype(&io_uring_free_probe)>{io_uring_get_probe(),
                                                                           io_uring_free_probe};
}

lib::Result<void, std::string_view> uring_ops_check() {
    auto probe = uring_probe();

#define OP_REQUIRED(OP)                                                                            \
    do {                                                                                           \
        if (!io_uring_opcode_supported(probe.get(), (OP)))                                         \
            return lib::Err{#OP};                                                                  \
    } while (false)

    OP_REQUIRED(IORING_OP_READ);
#undef OP_REQUIRED

    return {};
}

lib::Result<void, std::error_code> ulimit_init(unsigned entries) {
    auto limit_maximize = [](int resource) -> lib::Result<rlimit, std::error_code> {
        rlimit ret;

        SC_TRY_ERRNO(getrlimit(resource, &ret));
        ret.rlim_cur = ret.rlim_max;
        SC_TRY_ERRNO(setrlimit(resource, &ret));

        return lib::Ok{ret};
    };

    auto memlock = SC_TRY(limit_maximize(RLIMIT_MEMLOCK));
    if (memlock.rlim_cur <= entries * ENTRY_SIZE) {
        std::cerr << "The memlock limit (" << memlock.rlim_cur << ") is too small. The ring will "
                  << "probably fail to open. Either raise the limit, or construct the ring with "
                  << "fewer entries.\n";
    }

    auto nofile = SC_TRY(limit_maximize(RLIMIT_NOFILE));
    if (nofile.rlim_cur <= net::ConnectionPool::SIZE * EXPECTED_FILES_PER_CONNECTION) {
        std::cerr
            << "The open file limit (" << nofile.rlim_cur << ") is too small. Large numbers "
            << "of concurrent connections will probably cause \"too many open files\" errors.";
    }

    return {};
}

unsigned uring_supported_ring_flags(KernelVersion version) {
    auto     probe = uring_probe();
    unsigned flags = 0;

    if (version >= KernelVersion{5, 18})
        flags |= IORING_SETUP_SUBMIT_ALL;
    if (version >= KernelVersion{5, 19})
        flags |= IORING_SETUP_COOP_TASKRUN;
    if (version >= KernelVersion{6, 0})
        flags |= IORING_SETUP_SINGLE_ISSUER;

    return flags;
}

lib::Result<void, std::error_code> uring_init(KernelVersion version, unsigned entries,
                                              io_uring& uring) {
    bool            done = false;
    io_uring_params params{.flags = uring_supported_ring_flags(version)};
    int             rc;
    for (unsigned size = entries; size >= 512; size /= 2) {
        if ((rc = io_uring_queue_init_params(size, &uring, &params)) == 0) {
            done = true;
            break;
        }
    }

    if (!done) {
        lib::Err err{std::make_error_code(std::errc{-rc})};
        std::cerr << "Failed to open ring: " << err.message() << ".\n";
        return err;
    }

#define FEAT_REQUIRED(F)                                                                           \
    do {                                                                                           \
        if (!(params.flags & (F))) {                                                               \
            std::cerr << "Required feature flag " #F " not supported by this kernel.\n";           \
            return lib::Err{std::make_error_code(std::errc::operation_not_supported)};             \
        }                                                                                          \
    } while (false)

    FEAT_REQUIRED(IORING_FEAT_SUBMIT_STABLE);
    FEAT_REQUIRED(IORING_FEAT_CQE_SKIP);
    FEAT_REQUIRED(IORING_FEAT_LINKED_FILE);
#undef FEAT_REQUIRED

    return {};
}

} // namespace

Uring::Uring(unsigned entries) {
    auto maybe_version = kernel_version();
    if (maybe_version.is_err()) {
        std::cerr << "Unable to check kernel version: " << maybe_version.assume_err().message()
                  << ".\n";
        std::abort();
    }

    auto version = maybe_version.as_ref().assume_ok();
    if (version < MIN_KERNEL_VERSION) {
        std::cerr << "Kernel version " << version.first << "." << version.second
                  << " is too old to be usable.\n";
        std::abort();
    }

    if (auto ops = uring_ops_check(); ops.is_err()) {
        std::cerr << "Required io_uring operation " << ops.assume_err()
                  << " is not available with this kernel (" << version.first << "."
                  << version.second << ").\n";
        std::abort();
    }

    if (auto limit_result = ulimit_init(entries); limit_result.is_err()) {
        std::cerr << "Unable to configure resource limits: " << limit_result.assume_err().message()
                  << ".\n";
        std::abort();
    }

    if (auto result = uring_init(version, entries, m_uring); result.is_err()) {
        std::cerr << "Unable to initialize ring: " << result.assume_err().message() << ".\n";
        std::abort();
    }
}

} // namespace sc::io::backend::uring
