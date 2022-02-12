/*
 * UTIL -- Miscellaneous utility macros and definitions.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This file is licensed under the BSD 3-clause license. See the LICENSE file in
 * the project root for details.
 */

#pragma once

#include <cstdlib>

#define A3_NO_COPY(T)                                                                              \
public:                                                                                            \
    T(T const&) = delete;                                                                          \
    T& operator=(T const&) = delete

#define A3_NO_MOVE(T)                                                                              \
public:                                                                                            \
    T(T&&)     = delete;                                                                           \
    T& operator=(T&&) = delete

#define A3_PINNED(T)                                                                               \
    A3_NO_COPY(T);                                                                                 \
    A3_NO_MOVE(T)

namespace a3 {

struct MallocDeleter {
    void operator()(void* ptr) { free(ptr); }
};

} // namespace a3
