/*
 * DEFER — Run function on scope exit.
 *
 * Copyright (c) 2022, 2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <concepts>
#include <functional>

#include <a3/util.hh>

#include "sc/lib/fwd.hh"

namespace sc::lib::detail {

template <std::invocable Fn>
struct Defer {
    A3_PINNED(Defer);

private:
    Fn m_fn;

public:
    // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
    Defer(auto&& fn) : m_fn{SC_FWD(fn)} {}
    ~Defer() { std::invoke(std::move(*this).m_fn); }
};

template <std::invocable Fn>
Defer(Fn&&) -> Defer<Fn>;

} // namespace sc::lib::detail

#define SC_DEFER const ::sc::lib::detail::Defer _sc_defer_##__COUNTER__ = [&]()
