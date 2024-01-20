/*
 * SQE -- An awaitable io_uring submission.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cassert>
#include <climits>
#include <coroutine>
#include <cstdint>
#include <expected>
#include <system_error>
#include <type_traits>

#include <liburing.h>

#include "sc/lib/option.hh"

module sc.io.impl.uring.sqe;

import sc.lib.cast;
import sc.io.buf;

namespace sc::io::impl::uring {

Result::Result(io_uring_cqe const& cqe) noexcept :
    m_res{cqe.res},
    m_buf{SC_OPTION_IF(cqe.flags & IORING_CQE_F_BUFFER,
                       Buf::Id{lib::narrow_cast<std::underlying_type_t<Buf::Id>>(
                           cqe.flags >> IORING_CQE_BUFFER_SHIFT)})} {}

std::expected<std::uint32_t, Cqe::RequestFailed> Result::result() const noexcept {
    if (m_res < 0)
        return std::unexpected{Cqe::RequestFailed{std::error_code{-m_res, std::system_category()}}};
    return static_cast<std::uint32_t>(m_res);
}

SingleCqe::SingleCqe(io_uring_sqe& sqe) noexcept { io_uring_sqe_set_data(&sqe, this); }

SingleCqe::~SingleCqe() {
    if (m_handle)
        m_handle.destroy();
}

void SingleCqe::complete(io_uring_cqe const& cqe) noexcept {
    assert(!m_result);
    assert(m_handle);

    m_result.emplace(cqe);
    std::exchange(m_handle, {})();
}

bool SingleCqe::await_ready() const noexcept { return false; }

void SingleCqe::await_suspend(std::coroutine_handle<> handle) noexcept { m_handle = handle; }

Result SingleCqe::await_resume() noexcept {
    assert(m_result);
    return *m_result;
}

} // namespace sc::io::impl::uring
