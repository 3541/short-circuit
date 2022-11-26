/*
 * SHORT CIRCUIT: PIN — Macros for making types non-copyable/movable.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#define SC_NO_COPY(C)                                                                              \
public:                                                                                            \
    C(C const&) = delete;                                                                          \
    C& operator=(C const&) = delete

#define SC_NO_MOVE(C)                                                                              \
public:                                                                                            \
    C(C&&)     = delete;                                                                           \
    C& operator=(C&&) = delete

#define SC_PIN(C)                                                                                  \
    SC_NO_COPY(C);                                                                                 \
    SC_NO_MOVE(C)
