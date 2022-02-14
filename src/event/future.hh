/*
 * SHORT CIRCUIT: FUTURE -- Event orchestration.
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

#pragma once

#include <cstdlib>
#include <optional>

#include "coro.hh"

namespace sc::ev {

template <typename T>
class Future;

namespace detail {

class PromiseBase {
private:
    struct Final {
        constexpr bool await_ready() const noexcept { return false; }

        template <typename P>
        void await_suspend(std::coroutine_handle<P> coroutine) const noexcept {
            if (auto& caller = coroutine.promise().m_caller; caller)
                caller.resume();
        }

        constexpr void await_resume() const noexcept {}
    };

    std::coroutine_handle<> m_caller;

public:
    constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
    constexpr Final               final_suspend() const noexcept { return Final {}; }
    constexpr void set_caller(std::coroutine_handle<> coroutine) { m_caller = coroutine; }
};

template <typename T>
class Promise : public PromiseBase {
private:
    T m_value;

public:
    Future<T> get_return_object() {
        return Future { std::coroutine_handle<Promise>::from_promise(*this) };
    }

    template <std::convertible_to<T> V>
    constexpr void return_value(V&& value) {
        m_value = std::forward<V>(value);
    }

    constexpr T const& result() const& { return m_value; }
    constexpr T&       result() & { return m_value; }
    constexpr T        result() const&& { return std::move(m_value); }
};

template <>
class Promise<void> : public PromiseBase {
public:
    Future<void> get_return_object();

    constexpr void return_void() {}
    constexpr void result() {}
};

} // namespace detail

template <typename T>
class Future {
private:
    using Promise = detail::Promise<T>;

    struct Waiter {
        std::coroutine_handle<Promise> coroutine;

        bool await_ready() { return !coroutine || coroutine.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) {
            coroutine.promise().set_caller(caller);
            return coroutine;
        }
    };

    std::coroutine_handle<Promise> m_coroutine;

public:
    using promise_type = Promise;

    Future() = default;
    explicit Future(std::coroutine_handle<Promise> coroutine) : m_coroutine { coroutine } {}

    ~Future() {
        if (m_coroutine)
            m_coroutine.destroy();
    }

    Future(Future const&) = delete;
    Future& operator=(Future const&) = delete;
    Future(Future&& other) : m_coroutine(other.m_coroutine) { other.m_coroutine = nullptr; }
    Future& operator=(Future&& other) {
        m_coroutine       = other.m_coroutine;
        other.m_coroutine = nullptr;
        return *this;
    }

    auto operator co_await() const& {
        struct W : Waiter {
            decltype(auto) await_resume() { return this->coroutine.promise().result(); }
        };
        return W { m_coroutine };
    }

    auto operator co_await() const&& {
        struct W : Waiter {
            decltype(auto) await_resume() { return std::move(this->coroutine.promise()).result(); }
        };
        return W { m_coroutine };
    }

    bool done() { return m_coroutine.done(); }

    void start() { m_coroutine.resume(); }
};

namespace detail {

inline Future<void> Promise<void>::get_return_object() {
    return Future { std::coroutine_handle<Promise>::from_promise(*this) };
}

} // namespace detail

} // namespace sc::ev
