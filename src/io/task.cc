/*
 * SHORT CIRCUIT: TASK — An eagerly-started async task.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <utility>

#include <sc/io/task.hh>
#include <sc/lib/unreachable.hh>
#include <sc/shim/coro.hh>

namespace sc::io {

Task Task::Promise::get_return_object() {
    return Task{costd::coroutine_handle<Promise>::from_promise(*this)};
}

costd::suspend_never Task::Promise::initial_suspend() const noexcept { return {}; }

costd::suspend_never Task::Promise::final_suspend() const noexcept { return {}; }

void Task::Promise::return_void() const {}

void Task::Promise::unhandled_exception() const { lib::unreachable(); }

Task::Task(costd::coroutine_handle<Promise> handle) : m_handle{handle} {}

Task::Task(Task&& other) noexcept : m_handle{std::exchange(other.m_handle, nullptr)} {}

Task& Task::operator=(Task&& other) noexcept {
    std::exchange(*this, std::move(other));
    return *this;
}

void Task::cancel() && { m_handle.destroy(); }

} // namespace sc::io
