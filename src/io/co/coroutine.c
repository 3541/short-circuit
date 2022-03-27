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

#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <threads.h>
#include <ucontext.h>

#include <a3/log.h>
#include <a3/sll.h>
#include <a3/util.h>

#include <sc/coroutine.h>

#include "config.h"

#ifndef NDEBUG
#include <valgrind/memcheck.h>
#endif

typedef struct ScCoCtx {
    ucontext_t ctx;
} ScCoCtx;

typedef struct ScCoDeferred {
    ScCoDeferredCb f;
    void*          data;
    A3SLink        list;
} ScCoDeferred;

typedef struct ScCoroutine {
    uint8_t      stack[SC_CO_STACK_SIZE];
    ScCoCtx      ctx;
    A3SLL        deferred;
    ScCoCtx*     caller;
    ScEventLoop* ev;
    ssize_t      value;
#ifndef NDEBUG
    uint32_t vg_stack_id;
#endif
    bool done;
} ScCoroutine;

typedef void (*ScCoTrampoline)(void);

static thread_local size_t SC_CO_COUNT = 0;

ScCoCtx* sc_co_main_ctx_new() {
    A3_TRACE("Creating main coroutine context.");

    A3_UNWRAPNI(ScCoCtx*, ret, malloc(sizeof(*ret)));
    A3_UNWRAPSD(getcontext(&ret->ctx));
    return ret;
}

void sc_co_main_ctx_free(ScCoCtx* ctx) {
    assert(ctx);
    free(ctx);
}

// Standards-compliant ucontext requires that all arguments to the entry point be ints. On platforms
// where sizeof(int) == sizeof(void*), this works fine. Where sizeof(int) < sizeof(void*), annoying
// hackery is necessary to split and recombine pointers to/from ints. Thankfully, non-ancient
// versions of glibc make pointer-sized arguments work on 64-bit platforms.
#if UINT_MAX == UINTPTR_MAX ||                                                                     \
    (defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 8)) &&          \
     UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFULL)
#define SC_CO_BEGIN_ARGS(ENTRY, SELF, DATA)                                                        \
    3, (uintptr_t)(ENTRY), (uintptr_t)(SELF), (uintptr_t)(DATA)

static void sc_co_begin(ScCoEntry entry, ScCoroutine* self, void* data) {

#elif INT_MAX == INTPTR_MAX >> 32
#define SC_CO_P_SPLIT(P) (unsigned int)(uintptr_t)(P), (unsigned int)((uintptr_t)(P) >> 32)
#define SC_CO_BEGIN_ARGS(ENTRY, SELF, DATA)                                                        \
    6, SC_CO_P_SPLIT(ENTRY), SC_CO_P_SPLIT(SELF), SC_CO_P_SPLIT(DATA)

static void sc_co_begin(unsigned int entry_l, unsigned int entry_h, unsigned int self_l,
                        unsigned int self_h, unsigned int data_l, unsigned int data_h) {
#define SC_CO_P_COMBINE(L, H) (void*)((L) | (uintptr_t)(H) << 32);
    ScCoEntry    entry = SC_CO_P_COMBINE(entry_l, entry_h);
    ScCoroutine* self  = SC_CO_P_COMBINE(self_l, self_h);
    void*        data  = SC_CO_P_COMBINE(data_l, data_h);
#undef SC_CO_P_COMBINE

#else
#error "Unsupported pointer size."
#endif

    self->value = entry(self, data);
    self->done  = true;

    A3_SLL_FOR_EACH(ScCoDeferred, deferred, &self->deferred, list) { deferred->f(deferred->data); }
}

ScCoroutine* sc_co_new(ScCoCtx* caller, ScEventLoop* ev, ScCoEntry entry, void* data) {
    assert(caller);
    assert(ev);
    assert(entry);

    A3_UNWRAPNI(ScCoroutine*, ret, calloc(1, sizeof(*ret)));
    ret->ev     = ev;
    ret->caller = caller;
    ret->done   = false;
#ifndef NDEBUG
    ret->vg_stack_id = VALGRIND_STACK_REGISTER(ret->stack, ret->stack + sizeof(ret->stack));
#endif

    a3_sll_init(&ret->deferred);

    A3_UNWRAPSD(getcontext(&ret->ctx.ctx));

    ret->ctx.ctx.uc_stack = (stack_t) { .ss_sp = ret->stack, .ss_size = sizeof(ret->stack) };
    ret->ctx.ctx.uc_link  = &ret->caller->ctx;
    makecontext(&ret->ctx.ctx, (ScCoTrampoline)sc_co_begin, SC_CO_BEGIN_ARGS(entry, ret, data));

    SC_CO_COUNT++;
    return ret;
}

ScCoroutine* sc_co_spawn(ScCoroutine* caller, ScCoEntry entry, void* data) {
    assert(caller);
    assert(entry);

    return sc_co_new(&caller->ctx, caller->ev, entry, data);
}

ssize_t sc_co_yield(ScCoroutine* self) {
    assert(self);

    A3_UNWRAPSD(swapcontext(&self->ctx.ctx, &self->caller->ctx));
    return self->value;
}

static void sc_co_free(ScCoroutine* co) {
    assert(co);

#ifndef NDEBUG
    VALGRIND_STACK_DEREGISTER(co->vg_stack_id);
#endif

    SC_CO_COUNT--;
    free(co);
}

ssize_t sc_co_resume(ScCoroutine* co, ssize_t param) {
    assert(co);

    co->value = param;
    A3_UNWRAPSD(swapcontext(&co->caller->ctx, &co->ctx.ctx));

    ssize_t ret = co->value;
    if (co->done)
        sc_co_free(co);

    return ret;
}

void sc_co_defer(ScCoroutine* self, ScCoDeferredCb f, void* data) {
    assert(self);
    assert(f);

    A3_UNWRAPNI(ScCoDeferred*, def, calloc(1, sizeof(*def)));
    *def = (ScCoDeferred) {
        .f    = f,
        .data = data,
    };

    a3_sll_push(&self->deferred, &def->list);
}

size_t sc_co_count() { return SC_CO_COUNT; }

ScEventLoop* sc_co_event_loop(ScCoroutine* co) {
    assert(co);
    return co->ev;
}
