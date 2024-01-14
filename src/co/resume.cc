/*
 * RESUME -- An awaitable which resumes a coroutine handle.
 *
 * Copyright (c) 2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <coroutine>

module sc.co.resume;

namespace sc::co {

Resume::Resume(std::coroutine_handle<> handle) noexcept : m_handle{handle} {}
bool Resume::await_ready() const noexcept { return false; }
void Resume::await_resume() const noexcept {}

std::coroutine_handle<> Resume::await_suspend(std::coroutine_handle<>) const noexcept {
    return m_handle ?: std::noop_coroutine();
}

} // namespace sc::co
