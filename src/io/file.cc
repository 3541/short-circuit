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
#include <cstdint>
#include <expected>
#include <limits>
#include <utility>

module sc.io.file;

import sc.io.owner;

namespace sc::io {

template <Ownership O>
#ifdef SC_IO_BACKEND_FILE_FLAGS
FileBase<O>::FileBase(Descriptor fd, std::uint8_t flags) noexcept :
    m_fd{static_cast<int>(fd)}, m_flags{flags} {
#else
FileBase<O>::FileBase(Descriptor fd) noexcept : m_fd{static_cast<int>(fd)} {
#endif
    assert(m_fd <= std::numeric_limits<int>::max());
}

// clang-format off
template <Ownership O>
FileBase<O>::FileBase(FileBase&& other)
    requires(O == Ownership::Owned)
    : m_fd{std::exchange(other.m_fd, INVALID)}
#ifdef SC_IO_BACKEND_FILE_FLAGS
    , m_flags{other.m_flags}
#endif
{}
// clang-format on

template <Ownership O>
FileBase<O>::operator FileBase<Ownership::Borrowed>() const noexcept {
    assert(m_fd >= 0);

    return FileBase<Ownership::Borrowed>{static_cast<Descriptor>(m_fd)};
}

template <Ownership O>
FileBase<O>::operator Descriptor() const noexcept {
    assert(m_fd >= 0);

    return static_cast<Descriptor>(m_fd);
}

template <Ownership O>
FileBase<O>::operator int() const noexcept {
    return static_cast<int>(static_cast<unsigned>(*this));
}

#ifdef SC_IO_BACKEND_FILE_FLAGS
template <Ownership O>
FileBase<O>::operator std::pair<unsigned, std::uint8_t>() const noexcept {
    return {static_cast<unsigned>(*this), m_flags};
}

template <Ownership O>
bool FileBase<O>::flag(std::uint8_t flag) const noexcept {
    return m_flags & flag;
}
#endif

template <Ownership O>
SocketBase<O>::operator SocketBase<Ownership::Borrowed>() const noexcept {
    return SocketBase<Ownership::Borrowed>{static_cast<FileBase<O>::Descriptor>(*this)};
}

template struct FileBase<Ownership::Owned>;
template struct FileBase<Ownership::Borrowed>;
template struct SocketBase<Ownership::Owned>;
template struct SocketBase<Ownership::Borrowed>;

} // namespace sc::io
