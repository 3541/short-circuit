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
#include <cstdint>
#include <type_traits>
#include <utility>

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

Buf Allocator::alloc() noexcept {
    auto const id = m_free.back();
    m_free.pop_back();

    return get_unsafe(id);
}

Buf Allocator::get_unsafe(Buf::Id id) noexcept {
    auto const i     = std::to_underlying(id);
    auto const block = i / BLOCK_SIZE;
    auto const index = i % BLOCK_SIZE;

    return Buf{id, m_blocks[block][index].m_buf};
}

void Allocator::free(Buf&& buf) noexcept { m_free.push_back(buf.id()); }

Allocator::Group Allocator::group(Buf::Id id) noexcept {
    return Group{static_cast<std::uint16_t>(std::to_underlying(id) / BLOCK_SIZE)};
}

} // namespace sc::io
