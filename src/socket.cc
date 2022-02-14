/*
 * SHORT CIRCUIT: SOCKET
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include "socket.hh"

#include <utility>

#include <unistd.h>

namespace sc {

Socket::Socket(int fd) : m_fd { fd } {}

Socket::Socket(Socket&& other) noexcept : m_fd { std::exchange(other.m_fd, -1) } {}

Socket& Socket::operator=(Socket&& other) noexcept {
    Socket::~Socket();
    m_fd = std::exchange(other.m_fd, -1);
    return *this;
}

Socket::~Socket() {
    if (m_fd >= 0)
        close(m_fd);
}

int Socket::fd() const { return m_fd; }

int Socket::release() { return std::exchange(m_fd, -1); }

} // namespace sc
