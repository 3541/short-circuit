/*
 * POLLABLE -- An awaitable poll event.
 *
 * Copyright (c) 2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cassert>
#include <coroutine>
#include <expected>
#include <ostream>
#include <tuple>
#include <variant>

#include <poll.h>

module sc.io.impl.poll.pollable;

import sc.io.file;
import sc.lib.bind;

namespace sc::io::impl::poll {

Pollable::Pollable(Handler handler, Event event, FileRef file) noexcept :
    m_state{std::tuple{handler, event, file}} {}

bool Pollable::await_ready() const noexcept { return false; }

void Pollable::await_suspend(std::coroutine_handle<> handle) noexcept {
    assert((std::holds_alternative<std::tuple<Handler, Event, FileRef>>(m_state)));

    auto [handler, event, file] = std::get<0>(std::exchange(m_state, handle));
    handler(*this, event, file);
}

std::expected<Pollable::Event, Pollable::Error> Pollable::await_resume() noexcept {
    assert(std::holds_alternative<short>(m_state));

    auto const result = *std::get_if<short>(&m_state);
    if (result & (POLLERR | POLLNVAL)) [[unlikely]]
        return std::unexpected{(result & POLLNVAL) ? Error::Invalid : Error::Closed};

    return Event{result};
}

void Pollable::complete(short result) noexcept {
    assert(std::holds_alternative<std::coroutine_handle<>>(m_state));

    std::get<std::coroutine_handle<>>(std::exchange(m_state, result))();
}

PollFor::PollFor(Pollable::Handler handler, Pollable::Event event, FileRef file) noexcept :
    m_handler{handler}, m_event{event}, m_file{file} {}

Pollable PollFor::operator co_await() && noexcept { return Pollable{m_handler, m_event, m_file}; }

std::ostream& operator<<(std::ostream& stream, Pollable::Error error) {
    switch (error) {
    case Pollable::Error::Closed:
        return stream << "Error::Closed";
    case Pollable::Error::Invalid:
        return stream << "Error::Invalid";
    }
}

} // namespace sc::io::impl::poll
