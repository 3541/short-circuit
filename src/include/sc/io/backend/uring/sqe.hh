/*
 * SHORT CIRCUIT: SQE — Pre-submission io_uring operation.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

#include <liburing.h>

#include <sc/io/backend/uring/forward.hh>
#include <sc/lib/pin.hh>

namespace sc::io::backend::uring {

namespace detail {

template <typename Fn>
concept InnerSqeFn = std::is_invocable_r_v<io_uring_sqe&, Fn, Cqe&>;

} // namespace detail

template <typename Fn>
concept SqeFillFn = std::is_invocable_r_v<io_uring_sqe&, Fn>;

template <detail::InnerSqeFn Fn>
struct Sqe {
    SC_NO_COPY(Sqe);

private:
    Fn m_fill_fn;

    template <SqeFillFn F>
    friend auto make_sqe(F&& fn);

    explicit Sqe(Fn&&);

public:
    Sqe(Sqe&&) noexcept;
    Sqe& operator=(Sqe&&) noexcept;

    template <detail::InnerSqeFn OtherFn>
    auto then(Sqe<OtherFn>) &&;

    io_uring_sqe& fill(Cqe&) &&;
};

template <detail::InnerSqeFn Fn>
Sqe<Fn>::Sqe(Fn&& fn) : m_fill_fn{std::forward<Fn>(fn)} {}

template <detail::InnerSqeFn Fn>
Sqe<Fn>::Sqe(Sqe&&) noexcept = default;

template <detail::InnerSqeFn Fn>
Sqe<Fn>& Sqe<Fn>::operator=(Sqe&&) noexcept = default;

template <detail::InnerSqeFn Fn>
template <detail::InnerSqeFn OtherFn>
auto Sqe<Fn>::then(Sqe<OtherFn> other) && {
    return Sqe{[first = std::move(*this), second = std::move(other)](Cqe& cqe) {
        auto& first_sqe = std::move(first).fill(cqe);
        first_sqe.flags |= IOSQE_IO_LINK | IOSQE_CQE_SKIP_SUCCESS;

        return std::move(second).fill(cqe);
    }};
}

template <detail::InnerSqeFn Fn>
io_uring_sqe& Sqe<Fn>::fill(Cqe& cqe) && {
    return std::move(m_fill_fn)(cqe);
}

template <SqeFillFn Fn>
auto make_sqe(Fn&& fn) {
    // clang-format off
    return Sqe{[f = std::forward<Fn>(fn)](Cqe& cqe) mutable -> auto& {
        auto& sqe = std::forward<Fn>(f)();
        io_uring_sqe_set_data(&sqe, &cqe);
        return sqe;
    }}; // clang-format on
}

} // namespace sc::io::backend::uring
