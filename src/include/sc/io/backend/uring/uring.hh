/*
 * SHORT CIRCUIT: URING — io_uring event loop implementation.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cstddef>
#include <limits>
#include <span>

#include <liburing.h>

#include <sc/io/backend/uring/cqe.hh>
#include <sc/io/backend/uring/forward.hh>
#include <sc/io/backend/uring/sqe.hh>
#include <sc/io/file.hh>
#include <sc/lib/cast.hh>
#include <sc/lib/from.hh>

namespace sc::io::backend::uring {

struct Uring {
private:
    io_uring m_uring;

    io_uring_sqe& get_sqe();

public:
    explicit Uring(unsigned entries);

    auto read(FileRef, std::span<std::byte>, std::size_t offset = 0);

    template <typename F>
    static decltype(auto) future_transform(F&&);

    template <detail::InnerSqeFn Fn>
    static Cqe future_transform(Sqe<Fn>);
};

inline auto Uring::read(FileRef file, std::span<std::byte> buffer, std::size_t offset) {
    return make_sqe([=]() -> auto& {
        auto& sqe = get_sqe();

        io_uring_prep_read(&sqe, file, buffer.data(), lib::clamp_cast<unsigned>(buffer.size()),
                           offset);
        return sqe;
    });
}

template <typename F>
decltype(auto) Uring::future_transform(F&& future) {
    return std::forward<F>(future);
}

template <detail::InnerSqeFn Fn>
Cqe Uring::future_transform(Sqe<Fn> sqe) {
    Cqe ret{{}};
    std::move(sqe).fill(ret);

    return ret;
}

} // namespace sc::io::backend::uring
