/*
 * FILE -- RAII file descriptor wrapper.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cassert>

module sc.io.file;

import sc.io.owner;

namespace sc::io {

template <Ownership O>
FileBase<O>::FileBase(unsigned fd) noexcept : m_fd{fd} {
    assert(m_fd);
}

} // namespace sc::io
