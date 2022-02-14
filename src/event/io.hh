/*
 * SHORT CIRCUIT: IO -- IO future type.
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

#include <concepts>

#include "coro.hh"

namespace sc::ev {

template <typename I, typename R>
class IOBase {
private:
    std::coroutine_handle<> m_caller;
    R                       m_result;

protected:
    void complete(R result) {
        m_result = result;
        m_caller.resume();
    }

public:
    constexpr bool await_ready() const { return false; }

    void           await_suspend(std::coroutine_handle<> caller) { m_caller = caller; }
    decltype(auto) await_resume() { return static_cast<I*>(this)->result(); }

    R result() { return m_result; }
};

} // namespace sc::ev
