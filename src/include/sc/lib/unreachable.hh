/*
 * SHORT CIRCUIT: UNREACHABLE
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <version>

#ifdef __cpp_lib_unreachable
#include <utility>
#endif

namespace sc::lib {

[[noreturn]] inline void unreachable() {
#ifdef __cpp_lib_unreachable
    std::unreachable();
#elif defined(__GNUC__)
    __builtin_unreachable();
#else
#error "No unreachable() definition available."
#endif
}

} // namespace sc::lib
