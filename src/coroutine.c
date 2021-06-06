/*
 * SHORT CIRCUIT: COROUTINE -- Lightweight coroutines.
 *
 * Copyright (c) 2021, Alex O'Brien <3541ax@gmail.com>
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

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <valgrind/memcheck.h>

#include "a3/util.h"

typedef ucontext_t CoContext;

typedef struct {
    uint8_t    stack[16384];
    CoContext  context;
    CoContext* parent;
    size_t     result;
    uint32_t   vg_stack_id;
    bool       done;
} Coroutine;

typedef size_t (*CoEntry)(Coroutine*, void* data);

#define CO_PTR_SPLIT(P)           (int)(uintptr_t)(P), (int)((uintptr_t)(P) >> 32)
#define CO_PTR_COMBINE(LOW, HIGH) (void*)((uint32_t)(LOW) | (uintptr_t)(uint32_t)(HIGH) << 32)

#define CO_ENTRY_ARGS(ENTRY, CO, DATA) 6, CO_PTR_SPLIT(ENTRY), CO_PTR_SPLIT(CO), CO_PTR_SPLIT(DATA)

static void co_free(Coroutine* co) {
    assert(co);

    VALGRIND_STACK_DEREGISTER(co->vg_stack_id);
    free(co);
}

static void co_begin(int f_low, int f_high, int co_low, int co_high, int d_low, int d_high) {
    CoEntry    entry = CO_PTR_COMBINE(f_low, f_high);
    Coroutine* co    = CO_PTR_COMBINE(co_low, co_high);
    void*      data  = CO_PTR_COMBINE(d_low, d_high);

    co->result = entry(co, data);

    co->done = true;
    setcontext(co->parent);
}

static Coroutine* co_new(CoContext* parent, CoEntry entry, void* data) {
    assert(parent);
    assert(entry);

    Coroutine* ret;

    size_t size = sizeof(Coroutine) / 64 * 64 + (sizeof(Coroutine) % 64 != 0) * 64;
    A3_UNWRAPN(ret, aligned_alloc(64, size));
    ret->vg_stack_id = VALGRIND_STACK_REGISTER(ret->stack, &ret->stack[0] + sizeof(ret->stack));
    ret->done        = false;

    A3_UNWRAPSD(getcontext(&ret->context));
    ret->context.uc_stack.ss_sp   = &ret->stack;
    ret->context.uc_stack.ss_size = sizeof(ret->stack);
    ret->context.uc_link          = NULL;
    makecontext(&ret->context, (void*)co_begin, CO_ENTRY_ARGS(entry, ret, data));

    ret->parent = parent;

    return ret;
}

static size_t co_yield(Coroutine* co, size_t result) {
    assert(co);
    assert(!co->done);

    co->result = result;
    A3_UNWRAPSD(swapcontext(&co->context, co->parent));

    return co->result;
}

static size_t co_run(Coroutine* co) {
    assert(co);
    assert(!co->done);

    A3_UNWRAPSD(swapcontext(co->parent, &co->context));
    size_t result = co->result;

    return result;
}

static size_t co_f1(Coroutine* co, void* data) {
    assert(co);

    fprintf(stderr, "co_f1.\n");

    for (size_t i = 0; i < 10; i++)
        co_yield(co, i);

    return 0;
}

static size_t co_f2(Coroutine* co, void* data) {
    assert(co);

    fprintf(stderr, "co_f2.\n");

    for (size_t i = 0; i < 10; i++)
        co_yield(co, i);

    return 0;
}

int main(void) {
    CoContext main_ctx;
    A3_UNWRAPSD(getcontext(&main_ctx));

    Coroutine* c1 = co_new(&main_ctx, co_f1, NULL);
    Coroutine* c2 = co_new(&main_ctx, co_f2, NULL);

    while (!c1->done && !c2->done) {
        fprintf(stderr, "c1: %zu\n", co_run(c1));
        fprintf(stderr, "c2: %zu\n", co_run(c2));
    }


    co_free(c1);
    co_free(c2);

    fprintf(stderr, "Done.\n");
}
