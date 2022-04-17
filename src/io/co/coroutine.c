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

typedef struct ScCoMain {
    ScCoCtx      ctx;
    A3SLL        spawn_queue;
    ScEventLoop* ev;
    size_t       count;
} ScCoMain;

typedef struct ScCoDeferred {
    ScCoDeferredCb f;
    void*          data;
    A3SLink        list;
} ScCoDeferred;

typedef struct ScCoroutine {
    uint8_t   stack[SC_CO_STACK_SIZE];
    ScCoCtx   ctx;
    A3SLL     deferred;
    A3SLink   pending;
    ScCoMain* parent;
    ssize_t   value;
    size_t    extra_data;
#ifndef NDEBUG
    uint32_t vg_stack_id;
#endif
    bool done;
} ScCoroutine;

typedef void (*ScCoTrampoline)(void);

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

    assert(entry);
    assert(self);

    self->value = entry(self, data);

    A3_SLL_FOR_EACH(ScCoDeferred, deferred, &self->deferred, list) { deferred->f(deferred->data); }
    self->done = true;

    sc_co_yield(self);

    // NOTE: Returning from here will terminate the process.
}

static void sc_co_ctx_init(ScCoroutine* self, void* stack, size_t stack_size, ScCoEntry entry,
                           void* data) {
    assert(self);
    assert(stack);
    assert(stack_size);
    assert(entry);

    ucontext_t* ctx = &self->ctx.ctx;

    A3_UNWRAPSD(getcontext(ctx));
    ctx->uc_stack = (stack_t) { .ss_sp = stack, .ss_size = stack_size };
    ctx->uc_link  = NULL;
    makecontext(ctx, (ScCoTrampoline)sc_co_begin, SC_CO_BEGIN_ARGS(entry, self, data));
}

static void sc_co_ctx_swap(ScCoCtx* dst, ScCoCtx* src) {
    assert(dst);
    assert(src);

    A3_UNWRAPSD(swapcontext(&dst->ctx, &src->ctx));
}

ScCoMain* sc_co_main_new(ScEventLoop* ev) {
    A3_TRACE("Creating main coroutine context.");

    A3_UNWRAPNI(ScCoMain*, ret, malloc(sizeof(*ret)));
    A3_UNWRAPSD(getcontext(&ret->ctx.ctx));

    ret->ev    = ev;
    ret->count = 0;

    a3_sll_init(&ret->spawn_queue);

    return ret;
}

void sc_co_main_free(ScCoMain* main) {
    assert(main);

    free(main);
}

ScEventLoop* sc_co_main_event_loop(ScCoMain* main) {
    assert(main);

    return main->ev;
}

void sc_co_main_pending_resume(ScCoMain* main) {
    assert(main);

    for (A3SLink* l = a3_sll_dequeue(&main->spawn_queue); l;
         l          = a3_sll_dequeue(&main->spawn_queue)) {
        ScCoroutine* co = A3_CONTAINER_OF(l, ScCoroutine, pending);
        sc_co_resume(co, 0);
    }
}

size_t sc_co_count(ScCoMain* main) {
    assert(main);

    return main->count;
}

ScCoroutine* sc_co_new(ScCoMain* main, ScCoEntry entry, void* data) {
    assert(main);
    assert(entry);

    A3_UNWRAPNI(ScCoroutine*, ret, calloc(1, sizeof(*ret)));
    ret->parent     = main;
    ret->value      = 0;
    ret->done       = false;
    ret->extra_data = 0;
#ifndef NDEBUG
    ret->vg_stack_id = VALGRIND_STACK_REGISTER(ret->stack, ret->stack + sizeof(ret->stack));
#endif

    a3_sll_init(&ret->deferred);
    sc_co_ctx_init(ret, &ret->stack, sizeof(ret->stack), entry, data);

    main->count++;
    return ret;
}

ScCoroutine* sc_co_spawn(ScCoroutine* self, ScCoEntry entry, void* data) {
    assert(self);
    assert(entry);

    ScCoroutine* ret = sc_co_new(self->parent, entry, data);
    a3_sll_enqueue(&self->parent->spawn_queue, &ret->pending);

    return ret;
}

ssize_t sc_co_yield(ScCoroutine* self) {
    assert(self);

    sc_co_ctx_swap(&self->ctx, &self->parent->ctx);
    return self->value;
}

static void sc_co_free(ScCoroutine* co) {
    assert(co);

#ifndef NDEBUG
    VALGRIND_STACK_DEREGISTER(co->vg_stack_id);
#endif

    co->parent->count--;
    free(co);
}

ssize_t sc_co_resume(ScCoroutine* co, ssize_t param) {
    assert(co);

    co->value = param;
    sc_co_ctx_swap(&co->parent->ctx, &co->ctx);

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

ScEventLoop* sc_co_event_loop(ScCoroutine* co) {
    assert(co);
    return co->parent->ev;
}

void sc_co_extra_data_set(ScCoroutine* self, size_t data) {
    assert(self);

    self->extra_data = data;
}

size_t sc_co_extra_data_get(ScCoroutine* self) {
    assert(self);

    return self->extra_data;
}
