/*
 * ADDR -- Socket address types.
 *
 * Copyright (c) 2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <netinet/in.h>

module sc.io.addr;

namespace sc::io {

Port::Port(in_port_t port) noexcept : m_net_port{htons(port)} {}

Port::operator in_port_t() const noexcept { return m_net_port; }

Addr::Addr(in6_addr addr, Port port) noexcept :
    m_addr{.sin6_family = AF_INET6, .sin6_port = port, .sin6_addr = addr} {}

Addr Addr::any(Port port) noexcept { return Addr{IN6ADDR_ANY_INIT, port}; }

Addr::operator sockaddr const*() const noexcept {
    return reinterpret_cast<sockaddr const*>(&m_addr);
}

socklen_t Addr::size() const noexcept { return sizeof(m_addr); }

} // namespace sc::io
