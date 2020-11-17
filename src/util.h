#pragma once

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"

#define ALLOW_UNUSED  __attribute__((unused))
#define INLINE        static inline ALLOW_UNUSED
#define ALWAYS_INLINE INLINE __attribute__((always_inline))

// Abort with a message.
#define PANIC_FMT(fmt, ...)                                                    \
    do {                                                                       \
        ERR_FMT(fmt, __VA_ARGS__);                                             \
        exit(EXIT_FAILURE);                                                    \
    } while (0)

#define PANIC(msg) PANIC_FMT("%s", (msg))

#define UNREACHABLE() PANIC("UNREACHABLE")

// "unwrap" a return value which is falsy on error, and assign to T on success.
// This is useful for fatal errors (e.g., allocation failures).
#define UNWRAPN(T, X)                                                          \
    do {                                                                       \
        if (!(T = X)) {                                                        \
            PANIC_FMT("UNWRAP(%s)", #X);                                       \
        }                                                                      \
    } while (0)

// Unwrap a return value which is negative on error and ignore the result
// otherwise (i.e., unwrap-sign-discard).
#define UNWRAPSD(X)                                                            \
    do {                                                                       \
        if ((X) < 0) {                                                         \
            PANIC_FMT("UNWRAP(%s)", #X);                                       \
        }                                                                      \
    } while (0)

#define UNWRAPND(X)                                                            \
    do {                                                                       \
        if (!(X)) {                                                            \
            PANIC_FMT("UNWRAP(%s)", #X);                                       \
        }                                                                      \
    } while (0)

// Unwrap a signed return value and keep the result.
#define UNWRAPS(T, X)                                                          \
    do {                                                                       \
        if ((T = X) < 0) {                                                     \
            PANIC_FMT("UNWRAP(%s)", #X);                                       \
        }                                                                      \
    } while (0)

// Bubble up an error condition. Requires the caller to return a boolean and the
// callee to return a falsy value on failure.
#define TRYB(T)                                                                \
    do {                                                                       \
        if (!(T))                                                              \
            return false;                                                      \
    } while (0);

// Bubble up an error condition, mapping the error to the given value.
#define TRYB_MAP(T, E)                                                         \
    do {                                                                       \
        if (!(T))                                                              \
            return E;                                                          \
    } while (0);

// Map a truthy/falsy return to something else.
#define RET_MAP(F, T, E)                                                       \
    do {                                                                       \
        return F ? T : E;                                                      \
    } while (0);

#define FORMAT_FN(FMT_INDEX, VARG_INDEX)                                       \
    __attribute__((__format__(__printf__, FMT_INDEX, VARG_INDEX)))
