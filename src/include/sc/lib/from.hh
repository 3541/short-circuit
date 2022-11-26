/*
 * SHORT CIRCUIT: FROM — Conditionally-public function calls (see AK::Badge).
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <type_traits>

namespace sc::lib {

template <typename T>
struct From {
private:
    friend T;

    constexpr From()  = default;
    constexpr ~From() = default;

    From(From const&) = delete;
    From& operator=(From const&) = delete;

    From(From&&)  = delete;
    From& operator=(From&&) = delete;
};

#define SC_FROM() (::sc::lib::From<std::decay_t<decltype(*this)>>{})

} // namespace sc::lib
