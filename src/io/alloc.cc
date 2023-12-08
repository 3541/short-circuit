/*
 * ALLOC -- IO buffer allocator.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cstddef>
#include <type_traits>

module sc.io.alloc;

import sc.io.buf;

namespace sc::io {

Allocator::Allocator() noexcept : m_blocks{1} {
    for (std::underlying_type_t<Buf::Id> i = 0; i < BLOCK_SIZE; ++i) {
        m_free.push_back(Buf::Id{i});
    }
}

Allocator& Allocator::the() noexcept {
    static thread_local Allocator INSTANCE;
    return INSTANCE;
}

Buf Allocator::get_unsafe(Buf::Id id) noexcept {
    auto const i     = static_cast<std::size_t>(id);
    auto const block = i / 512;
    auto const index = i % 512;

    return Buf{id, m_blocks[block][index].m_buf};
}

void Allocator::free(Buf&& buf) noexcept { m_free.push_back(buf.id()); }

} // namespace sc::io
