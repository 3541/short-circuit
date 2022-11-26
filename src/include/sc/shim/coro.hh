/*
 * SHORT CIRCUIT: COROUTINE SHIM
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#if __has_include(<coroutine>)
#include <coroutine>
namespace sc {
namespace costd = std;
}
#elif __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
namespace sc {
namespace costd = std::experimental;
}
#else
#error "Coroutine support is required."
#endif
