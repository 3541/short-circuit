/*
 * SHORT CIRCUIT: DEFER — Run function on scope exit.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <concepts>

#include <sc/lib/pin.hh>

namespace sc::lib::detail {

template <std::invocable Fn>
struct Defer {
private:
    Fn m_fn;

public:
    // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
    explicit Defer(auto&& fn) : m_fn{std::forward<decltype(fn)>(fn)} {}
    ~Defer() { std::forward<Fn>(m_fn)(); }
};

template <std::invocable Fn>
Defer(Fn&&) -> Defer<Fn>;

} // namespace sc::lib::detail

#define SC_DEFER(F)                                                                                \
    const ::sc::lib::detail::Defer _sc_defer_##__LINE__ { (F) }
