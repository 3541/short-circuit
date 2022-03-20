/*
 * SHORT CIRCUIT: COROUTINE -- Single-threaded coroutines.
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

#include <stddef.h>

#include <a3/types.h>

#include <sc/forward.h>

typedef ssize_t (*ScCoEntry)(ScCoroutine* self, void* data);

typedef struct ScCoAwaitAny {
    size_t  index;
    ssize_t result;
} ScCoAwaitAny;

A3_EXPORT ScCoCtx*     sc_co_main_ctx_new(void);
A3_EXPORT void         sc_co_main_ctx_free(ScCoCtx*);
A3_EXPORT ScCoroutine* sc_co_new(ScCoCtx* caller, ScEventLoop*, ScCoEntry entry, void* data);
A3_EXPORT ssize_t      sc_co_yield(ScCoroutine*);
A3_EXPORT ssize_t      sc_co_resume(ScCoroutine* co, ssize_t);
A3_EXPORT size_t       sc_co_count(void);
A3_EXPORT ScEventLoop* sc_co_event_loop(ScCoroutine*);
