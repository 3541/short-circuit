/*
 * SHORT CIRCUIT: TASK — An eagerly-started async task.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sc/io/event_loop.hh>
#include <sc/lib/pin.hh>
#include <sc/shim/coro.hh>

namespace sc::io {

struct Task {
    SC_NO_COPY(Task);

private:
    struct Promise {
    public:
        Task                 get_return_object();
        costd::suspend_never initial_suspend() const noexcept;
        costd::suspend_never final_suspend() const noexcept;
        void                 return_void() const;
        void                 unhandled_exception() const;

        template <typename F>
        decltype(auto) await_transform(F&&);
    };

    costd::coroutine_handle<Promise> m_handle;

    explicit Task(costd::coroutine_handle<Promise>);

public:
    using promise_type = Promise;

    Task(Task&&) noexcept;
    Task& operator=(Task&&) noexcept;

    void cancel() &&;
};

template <typename F>
decltype(auto) Task::Promise::await_transform(F&& future) {
    return EventLoop::future_transform(std::forward<F>(future));
}

} // namespace sc::io
