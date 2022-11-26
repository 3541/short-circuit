/*
 * SHORT CIRCUIT: FILE — File descriptor type.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "sc/io/file.hh"

#include <iostream>
#include <new>
#include <utility>

#include <unistd.h>

namespace sc::io {

FileRef::FileRef(int fd) : m_fd{fd} {}

FileRef::operator int() const { return m_fd; }

File::File(int fd, bool owned) : m_fd{fd}, m_owned{owned} {}

File::~File() {
    if (m_fd < 0)
        return;

    if (!m_owned) {
        std::cerr << "Leak of unowned file: " << m_fd << ".\n";
        return;
    }

    close(m_fd);
    m_fd = -1;
}

File File::owned(int fd) { return File{fd, true}; }

File File::unowned(int fd) { return File{fd, false}; }

File::File(File&& other) noexcept : m_fd{other.m_fd}, m_owned{other.m_owned} { other.m_fd = -1; }

File& File::operator=(File&& other) noexcept {
    this->~File();
    new (this) File{std::move(other)};
    return *this;
}

// NOLINTNEXTLINE(readability-make-member-function-const)
int File::fd() { return m_fd; }

void File::set_owned(bool owned) { m_owned = owned; }

File::operator FileRef() const { return FileRef{m_fd}; }

} // namespace sc::io
