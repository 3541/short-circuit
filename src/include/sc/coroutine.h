/*
 * SHORT CIRCUIT: COROUTINE -- Single-threaded coroutines.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stddef.h>

#include <a3/cpp.h>
#include <a3/types.h>

#include <sc/forward.h>

A3_H_BEGIN

typedef ssize_t (*ScCoEntry)(void* data);
typedef void (*ScCoDeferredCb)(void* data);

A3_EXPORT ScCoMain*    sc_co_main_new(ScEventLoop*);
A3_EXPORT void         sc_co_main_free(ScCoMain*);
A3_EXPORT ScEventLoop* sc_co_main_event_loop(ScCoMain*);
A3_EXPORT void         sc_co_main_pending_resume(ScCoMain*);
A3_EXPORT size_t       sc_co_count(ScCoMain*);
A3_EXPORT ScCoroutine* sc_co_new(ScCoMain*, ScCoEntry entry, void* data);
A3_EXPORT ScCoroutine* sc_co_spawn(ScCoEntry entry, void* data);
A3_EXPORT ssize_t      sc_co_yield(void);
A3_EXPORT ssize_t      sc_co_resume(ScCoroutine* co, ssize_t);
A3_EXPORT void         sc_co_defer_on(ScCoroutine*, ScCoDeferredCb, void* data);
A3_EXPORT void         sc_co_defer(ScCoDeferredCb, void* data);
A3_EXPORT ScEventLoop* sc_co_event_loop(void);
A3_EXPORT ScCoroutine* sc_co_current(void);

A3_H_END
