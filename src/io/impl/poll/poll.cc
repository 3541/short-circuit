/*
 * POLL -- poll bindings.
 *
 * Copyright (c) 2024, Alex O'Brien <3541@3541.website>
 /
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cerrno>
#include <coroutine>
#include <expected>
#include <system_error>
#include <type_traits>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "sc/lib/try.hh"

module sc.io.impl.poll;

import sc.co.future;
import sc.io.alloc;
import sc.io.file;
import sc.lib.bind;
import sc.lib.error;

namespace sc::io::impl::poll {

Poll::Poll() = default;

void Poll::wait(Pollable& pollable, Pollable::Event event, FileRef file) noexcept {
    m_waiting.push_back(pollable, pollfd{.fd = file, .events = std::to_underlying(event)});
}

co::Future<std::expected<std::size_t, Poll::Error>>
Poll::wait_for(FileRef file, Pollable::Event event, auto const& attempt)
    requires std::is_nothrow_invocable_r_v<ssize_t, decltype(attempt)>
{
    while (true) {
        auto const res = attempt();
        if (res >= 0)
            co_return static_cast<std::size_t>(res);

        switch (errno) {
        case EINTR:
            continue;
        case EAGAIN:
#if EAGAIN != EWOULDBLOCK
        case EWOULDBLOCK:
#endif
            SC_CO_TRY((co_await PollFor{lib::Bound{&Poll::wait, *this}, event, file}));
            continue;
        default:
            co_return std::unexpected{RequestFailed::from_errno()};
        }
    };
}

co::Nop<std::expected<void, Poll::Error>> Poll::nop() noexcept { return {}; }

co::Nop<std::expected<Socket, Poll::Errors::RequestFailed>> Poll::socket() noexcept {
    return Socket{
        SC_TRY(lib::error::sys(::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))};
}

co::Generator<std::expected<Socket, Poll::Error>> Poll::accept(SocketRef socket) noexcept {
    while (true) {
        auto const res =
            SC_CO_GEN_TRY(co_await wait_for(socket, Pollable::Event::Read, [socket]() noexcept {
                return ::accept4(socket, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
            }));

        co_yield Socket{static_cast<Socket::Descriptor>(res)};
    }
}

co::Future<std::expected<Buf, Poll::Error>> Poll::recv(SocketRef socket) noexcept {
    auto buf = Allocator::the().alloc();

    auto const len =
        SC_CO_TRY(co_await wait_for(socket, Pollable::Event::Read, [socket, b = *buf]() noexcept {
            return ::recv(socket, b.data(), b.size(), 0);
        }));

    co_return std::move(buf).slice(len);
}

co::Future<std::expected<std::size_t, Poll::Error>>
Poll::send_raw(SocketRef socket, std::span<std::byte const> buf) noexcept {
    co_return co_await wait_for(socket, Pollable::Event::Write, [socket, buf]() noexcept {
        return ::send(socket, buf.data(), buf.size(), 0);
    });
}

std::expected<void, Poll::Errors::RequestFailed> Poll::close(File::Descriptor file) noexcept {
    return lib::error::sys(::close(static_cast<int>(file)))
        .transform([](auto const&) {})
        .transform_error([](std::error_code error) { return Errors::RequestFailed{error}; });
}

void Poll::pump(bool wait) noexcept {
    std::ranges::swap(m_waiting, m_handling);

    lib::error::must(::poll(m_handling.data<pollfd>(), m_handling.len(), wait ? -1 : 0));

    for (auto&& [pollable, pollfd] : m_handling) {
        if (!pollfd.revents) {
            m_waiting.push_back(pollable, pollfd);
            continue;
        }

        pollable.complete(pollfd.revents);
    }

    m_handling.clear();
}

} // namespace sc::io::impl::poll
