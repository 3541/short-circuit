/*
 * SHORT CIRCUIT: CONNECTION POOL — Persistent connection pool.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cstddef>

namespace sc::io::net {

struct ConnectionPool {
public:
    static constexpr std::size_t SIZE = 1280;
};

} // namespace sc::io::net
