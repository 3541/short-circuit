/*
 * SHORT CIRCUIT: CAST — Utilities for casting between integral types.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <algorithm>
#include <concepts>
#include <limits>

namespace sc::lib {

template <typename Larger, typename Smaller>
concept FitsIn = requires(Larger larger, Smaller smaller) {
    requires std::integral<Larger>;
    requires std::integral<Smaller>;
    requires(std::signed_integral<Smaller> ? std::signed_integral<Larger> : true);
    requires(std::signed_integral<Larger> ? sizeof(Smaller) < sizeof(Larger)
                                          : sizeof(Smaller) <= sizeof(Larger));
};

template <std::integral To, std::integral From>
requires FitsIn<From, To>
constexpr To clamp_cast(From from) {
    return static_cast<To>(std::clamp(from, static_cast<From>(std::numeric_limits<To>::min()),
                                      static_cast<From>(std::numeric_limits<To>::max())));
}

} // namespace sc::lib
