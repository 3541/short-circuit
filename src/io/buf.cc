/*
 * BUF -- IO buffer.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cstddef>
#include <utility>

module sc.io.buf;

import sc.io.alloc;

namespace sc::io {

Buf::~Buf() {
    if (m_buf.empty())
        return;

    Allocator::the().free(std::move(*this));
}

Buf::Buf(Buf&& other) noexcept : m_buf{std::exchange(other.m_buf, {})}, m_id{other.m_id} {}

Buf& Buf::operator=(Buf&& other) noexcept {
    m_buf = std::exchange(other.m_buf, {});
    m_id  = other.m_id;
    return *this;
}

Buf::Id Buf::id() const noexcept { return m_id; }

Buf Buf::slice(std::size_t len) && noexcept {
    return Buf{m_id, std::exchange(m_buf, {}).subspan(0, len)};
}

} // namespace sc::io
