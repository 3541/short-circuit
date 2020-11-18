#pragma once

#include <assert.h>
#include <stdlib.h>

#include "util.h"

#define PQ(TY) struct TY##Pq

#define PQ_IMPL_STRUCT(TY)                                                     \
    PQ(TY) {                                                                   \
        TY*    data;                                                           \
        size_t len;                                                            \
        size_t cap;                                                            \
        size_t max_cap;                                                        \
    };

#define PQ_INIT(TY)    TY##_pq_init
#define PQ_PEEK(TY)    TY##_pq_peek
#define PQ_GET(TY)     TY##_pq_get
#define PQ_SET(TY)     TY##_pq_set
#define PQ_ENQUEUE(TY) TY##_pq_enqueue
#define PQ_HEAPIFY(TY) TY##_pq_heapify
#define PQ_DEQUEUE(TY) TY##_pq_dequeue

INLINE size_t pq_parent(size_t i) { return i / 2; }
INLINE size_t pq_left_child(size_t i) { return i * 2; }
INLINE size_t pq_right_child(size_t i) { return i * 2 + 1; }

#define PQ_DECLARE_METHODS(TY)                                                 \
    void PQ_INIT(TY)(PQ(TY)*, size_t initial_cap, size_t max_cap);             \
    TY*  PQ_PEEK(TY)(PQ(TY)*);                                                 \
    void PQ_ENQUEUE(TY)(PQ(TY)*, TY);                                          \
    void PQ_HEAPIFY(TY)(PQ(TY)*, size_t i);                                    \
    TY   PQ_DEQUEUE(TY)(PQ(TY)*)

#define PQ_IMPL_METHODS(TY, C)                                                 \
    void PQ_INIT(TY)(PQ(TY) * this, size_t initial_cap, size_t max_cap) {      \
        assert(!this->data);                                                   \
        UNWRAPN(this->data, calloc(initial_cap, sizeof(TY)));                  \
        this->len     = 0;                                                     \
        this->cap     = initial_cap;                                           \
        this->max_cap = max_cap;                                               \
    }                                                                          \
                                                                               \
    TY* PQ_PEEK(TY)(PQ(TY) * this) {                                           \
        assert(this);                                                          \
        if (this->len == 0)                                                    \
            return NULL;                                                       \
        return this->data;                                                     \
    }                                                                          \
                                                                               \
    /* Convenience functions for 1-based indexing. */                          \
    INLINE TY* PQ_GET(TY)(PQ(TY) * this, size_t i) {                           \
        assert(this);                                                          \
        assert(i > 0);                                                         \
        assert(i - 1 < this->len);                                             \
        return &this->data[i - 1];                                             \
    }                                                                          \
                                                                               \
    INLINE void PQ_SET(TY)(PQ(TY) * this, size_t i, TY item) {                 \
        assert(this);                                                          \
        assert(i > 0);                                                         \
        assert(i - 1 < this->cap);                                             \
        this->data[i - 1] = item;                                              \
    }                                                                          \
                                                                               \
    void PQ_ENQUEUE(TY)(PQ(TY) * this, TY item) {                              \
        assert(this);                                                          \
        assert(this->len + 1 < this->cap);                                     \
                                                                               \
        size_t i = ++this->len;                                                \
        while (i > 1 && C(PQ_GET(TY)(this, pq_parent(i)), &item) > 0) {        \
            PQ_SET(TY)(this, i, *PQ_GET(TY)(this, pq_parent(i)));              \
            i = pq_parent(i);                                                  \
        }                                                                      \
        PQ_SET(TY)(this, i, item);                                             \
    }                                                                          \
                                                                               \
    void PQ_HEAPIFY(TY)(PQ(TY) * this, size_t i) {                             \
        assert(this);                                                          \
                                                                               \
        TY item = *PQ_GET(TY)(this, i);                                        \
        while (i < this->len / 2) {                                            \
            size_t min_child = pq_left_child(i);                               \
            if (min_child < this->len &&                                       \
                C(PQ_GET(TY)(this, min_child),                                 \
                  PQ_GET(TY)(this, min_child + 1)) > 0)                        \
                min_child++;                                                   \
            if (C(&item, PQ_GET(TY)(this, min_child)) <= 0)                    \
                break;                                                         \
            PQ_SET(TY)(this, i, *PQ_GET(TY)(this, min_child));                 \
            i = min_child;                                                     \
        }                                                                      \
        PQ_SET(TY)(this, i, item);                                             \
    }                                                                          \
                                                                               \
    TY PQ_DEQUEUE(TY)(PQ(TY) * this) {                                         \
        assert(this);                                                          \
        assert(this->len > 0);                                                 \
                                                                               \
        TY ret = *PQ_PEEK(TY)(this);                                           \
        PQ_SET(TY)(this, 1, *PQ_GET(TY)(this, this->len--));                   \
        PQ_HEAPIFY(TY)(this, 1);                                               \
                                                                               \
        return ret;                                                            \
    }
