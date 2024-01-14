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
#include <ostream>
#include <span>
#include <string_view>
#include <utility>

module sc.io.buf;

import sc.io.alloc;

namespace sc::io {

Buf::Buf(Id id, std::span<std::byte> buf) noexcept : m_buf{buf}, m_id{id} {}

Buf::~Buf() {
    if (m_buf.empty())
        return;

    m_buf = {};
    Allocator::the().free(std::move(*this));
}

Buf::Buf(Buf&& other) noexcept : m_buf{std::exchange(other.m_buf, {})}, m_id{other.m_id} {}

Buf& Buf::operator=(Buf&& other) noexcept {
    this->~Buf();
    return *new (this) Buf{std::move(other)};
}

Buf::Id Buf::id() const noexcept { return m_id; }

std::pair<Buf::Id, std::span<std::byte>> Buf::release() && noexcept {
    return {m_id, std::exchange(m_buf, {})};
}

Buf Buf::slice(std::size_t len) && noexcept {
    return Buf{m_id, std::exchange(m_buf, {}).subspan(0, len)};
}

std::string_view Buf::as_str() const noexcept {
    return {reinterpret_cast<char const*>(m_buf.data()), m_buf.size()};
}

std::ostream& operator<<(std::ostream& stream, Buf const& buf) {
    return stream << "Buf(" << std::to_underlying(buf.m_id) << ") { " << buf.m_buf.size() << " @ "
                  << static_cast<void*>(buf.m_buf.data()) << " }";
}

} // namespace sc::io
