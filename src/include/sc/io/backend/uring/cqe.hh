/*
 * SHORT CIRCUIT: SQE — Submitted, awaitable io_uring operation.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cstdint>
#include <utility>

#include <liburing.h>

#include <sc/io/backend/uring/forward.hh>
#include <sc/lib/from.hh>
#include <sc/shim/coro.hh>

namespace sc::io::backend::uring {

struct Cqe {
private:
    union {
        costd::coroutine_handle<> m_handle;
        std::int32_t              m_result{-1};
    };

public:
    constexpr explicit Cqe(lib::From<Uring>);

    constexpr bool         await_ready() const;
    constexpr void         await_suspend(costd::coroutine_handle<>);
    constexpr std::int32_t await_resume() const;

    void resume(lib::From<Uring>, std::int32_t result);
};

constexpr Cqe::Cqe(lib::From<Uring>){};

constexpr bool Cqe::await_ready() const { return false; }

constexpr void Cqe::await_suspend(costd::coroutine_handle<> handle) { m_handle = handle; }

constexpr std::int32_t Cqe::await_resume() const { return m_result; }

} // namespace sc::io::backend::uring
