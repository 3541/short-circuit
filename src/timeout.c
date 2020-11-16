#include "timeout.h"

PQ_IMPL_STRUCT(Timeout);
PQ_DECLARE_METHODS(Timeout);

INLINE size_t timeout_compare(Timeout* lhs, Timeout* rhs) {
    assert(lhs);
    assert(rhs);
    return lhs->threshold - rhs->threshold;
}

PQ_IMPL_METHODS(Timeout, timeout_compare);
