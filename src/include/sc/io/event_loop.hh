/*
 * SHORT CIRCUIT: EVENT LOOP — Backend-agnostic event loop interface.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include SC_IO_IMPL_HEADER

namespace sc::io {

struct EventLoop {
private:
    backend::EventLoopImpl m_impl;

public:
    template <typename F>
    static decltype(auto) future_transform(F&&);
};

template <typename F>
decltype(auto) EventLoop::future_transform(F&& future) {
    return backend::EventLoopImpl::future_transform(std::forward<F>(future));
}

} // namespace sc::io
