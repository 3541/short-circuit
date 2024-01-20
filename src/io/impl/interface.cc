/*
 * INTERFACE -- Platform-agnostic IO interface.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cassert>
#include <coroutine>
#include <expected>
#include <span>

#include <netinet/in.h>
#include <sys/socket.h>

#include "sc/lib/try.hh"

module sc.io.interface;

import sc.config;
import sc.co.future;
import sc.io.file;
import sc.lib.error;

namespace sc::io {

Interface& Interface::the() noexcept {
    static thread_local Interface INSTANCE;
    return INSTANCE;
}

co::Future<std::expected<Socket, Interface::Error>>
Interface::listen_socket(Addr const& addr) noexcept {
    co_return (co_await impl::Interface::socket()).transform([&](Socket socket) {
        auto const fd = static_cast<int>(socket);

        int flag = 1;
        lib::error::must(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)));

        flag = 0;
        lib::error::must(::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &flag, sizeof(flag)));
        lib::error::must(::bind(fd, addr, addr.size()));
        lib::error::must(::listen(fd, config::LISTEN_BACKLOG));

        return socket;
    });
}

co::Future<std::expected<void, Interface::Error>>
Interface::send(SocketRef socket, std::span<std::byte const> buf) noexcept {
    while (!buf.empty()) {
        auto const len = SC_CO_TRY(co_await send_raw(socket, buf));
        assert(len <= buf.size());

        buf = buf.subspan(len);
    }

    co_return {};
}
} // namespace sc::io
