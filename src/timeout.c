#include "timeout.h"

LPQ_DECLARE_METHODS(Timeout);

INLINE ssize_t timeout_compare(Timeout* lhs, Timeout* rhs) {
    assert(lhs);
    assert(rhs);
    return lhs->threshold - rhs->threshold;
}

LPQ_IMPL_METHODS(Timeout, timeout_compare);
