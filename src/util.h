#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "log.h"

// Abort with a message.
#define PANIC_FMT(fmt, ...)                                                    \
    do {                                                                       \
        ERR_FMT(fmt, __VA_ARGS__);                                             \
        exit(EXIT_FAILURE);                                                    \
    } while (0)

#define PANIC(msg)                                                             \
    do {                                                                       \
        PANIC_FMT("%s", (msg));                                                \
    } while (0)

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

// Unwrap a signed return value and keep the result.
#define UNWRAPS(T, X)                                                          \
    do {                                                                       \
        if ((T = X) < 0) {                                                     \
            PANIC_FMT("UNWRAP(%s)", #X);                                       \
        }                                                                      \
    } while (0)
