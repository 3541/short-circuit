/*
 * SHORT CIRCUIT: PROMISE — Internal promise type used by Future<T> and Stream<T>.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This type is purely an implementation detail of Stream<T> and Future<T>. It should not be used
 * directly.
 */

#pragma once

#include <optional>
#include <utility>

#include <sc/io/event_loop.hh>
#include <sc/lib/unreachable.hh>
#include <sc/shim/coro.hh>

namespace sc::io::detail {

struct PromiseBase {
private:
    costd::coroutine_handle<> m_caller;

public:
    constexpr costd::suspend_always initial_suspend() const noexcept;
    constexpr auto                  final_suspend() const noexcept;
    inline void                     unhandled_exception() const;

    decltype(auto) await_transform(auto&&);
    void           set_caller(costd::coroutine_handle<>);
};

template <template <typename> typename F, typename T>
struct Promise : public PromiseBase {
private:
    std::optional<T> m_result;

public:
    F<T> get_return_object();

    void           return_value(std::convertible_to<T> auto&&);
    constexpr bool has_result() const;
    T              result();
};

template <template <typename> typename F>
struct Promise<F, void> : public PromiseBase {
public:
    F<void> get_return_object();

    constexpr void return_void() const;
    void           result();
};

constexpr costd::suspend_always PromiseBase::initial_suspend() const noexcept { return {}; }

constexpr auto PromiseBase::final_suspend() const noexcept {
    struct ReturnToCaller {
        costd::coroutine_handle<> m_caller;

        constexpr bool await_ready() const noexcept { return false; }

        constexpr costd::coroutine_handle<> await_suspend(costd::coroutine_handle<>) noexcept {
            return m_caller ?: costd::noop_coroutine();
        }

        constexpr void await_resume() noexcept {}
    };

    return ReturnToCaller{m_caller};
}

inline void PromiseBase::unhandled_exception() const { lib::unreachable(); }

decltype(auto) PromiseBase::await_transform(auto&& future) {
    return EventLoop::future_transform(std::forward<decltype(future)>(future));
}

inline void detail::PromiseBase::set_caller(costd::coroutine_handle<> caller) { m_caller = caller; }

template <template <typename> typename F, typename T>
F<T> Promise<F, T>::get_return_object() {
    return F<T>{costd::coroutine_handle<Promise>::from_promise(*this)};
}

template <template <typename> typename F, typename T>
void Promise<F, T>::return_value(std::convertible_to<T> auto&& value) {
    assert(!m_result.has_value());

    m_result.emplace(std::forward<decltype(value)>(value));
}

template <template <typename> typename F, typename T>
T Promise<F, T>::result() {
    assert(m_result.has_value());

    return *std::exchange(m_result, {});
}

template <template <typename> typename F, typename T>
constexpr bool Promise<F, T>::has_result() const {
    return m_result.has_value();
}

template <template <typename> typename F>
F<void> Promise<F, void>::get_return_object() {
    return F<void>{costd::coroutine_handle<Promise>::from_promise(*this)};
}

template <template <typename> typename F>
constexpr void Promise<F, void>::return_void() const {}

} // namespace sc::io::detail
