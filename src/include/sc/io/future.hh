/*
 * SHORT CIRCUIT: FUTURE — A generic future.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cassert>
#include <utility>

#include <sc/io/promise.hh>
#include <sc/lib/defer.hh>
#include <sc/lib/pin.hh>
#include <sc/lib/unreachable.hh>
#include <sc/shim/coro.hh>

namespace sc::io {

template <typename T = void>
struct Future {
    SC_NO_COPY(Future);

private:
    using Promise = detail::Promise<Future, T>;
    friend Promise;

    costd::coroutine_handle<Promise> m_handle;

    constexpr explicit Future(costd::coroutine_handle<Promise>);

public:
    using promise_type = Promise;

    constexpr Future(Future&&) noexcept;
    constexpr Future& operator=(Future&&) noexcept;

    constexpr bool            await_ready() const;
    costd::coroutine_handle<> await_suspend(costd::coroutine_handle<>);
    T                         await_resume();
};

template <typename T>
constexpr Future<T>::Future(costd::coroutine_handle<Promise> handle) : m_handle{handle} {}

template <typename T>
constexpr Future<T>::Future(Future&& other) noexcept :
    m_handle{std::exchange(other.m_handle, nullptr)} {}

template <typename T>
constexpr Future<T>& Future<T>::operator=(Future&& other) noexcept {
    std::exchange(*this, other);

    return *this;
}

template <typename T>
constexpr bool Future<T>::await_ready() const {
    return false;
}

template <typename T>
costd::coroutine_handle<> Future<T>::await_suspend(costd::coroutine_handle<> caller) {
    assert(m_handle);

    m_handle.promise().set_caller(caller);
    return m_handle;
}

template <typename T>
T Future<T>::await_resume() {
    SC_DEFER([this] {
        m_handle.destroy();
        m_handle = nullptr;
    });

    return m_handle.promise().result();
}

template <>
inline void Future<void>::await_resume() {
    m_handle.destroy();
    m_handle = nullptr;
}

} // namespace sc::io
