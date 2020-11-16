#pragma once

#define HEAP(TY) struct TY##Heap

#define HEAP_IMPL_STRUCT(TY)                                                   \
    HEAP(TY) {                                                                 \
        TY*    data;                                                           \
        size_t cap;                                                            \
        size_t len;                                                            \
    };

#define HEAP_INIT(TY) TY##_heap_init
#define HEAP_PEEK(TY) TY##_heap_peek

#define HEAP_DECLARE_METHODS(TY)                                               \
    void HEAP_INIT(TY)(HEAP(TY)*, size_t cap);                                 \
    TY*  HEAP_PEEK(TY)(HEAP(TY)*)

#define HEAP_IMPL_METHODS(TY)                                                  \
    void HEAP_INIT(TY)(HEAP(TY) * this, size_t cap) {                          \
        assert(!this->data);                                                   \
        UNWRAPN(this->data, calloc(cap, sizeof(TY)));                          \
        this->cap = cap;                                                       \
        this->len = 0;                                                         \
    }                                                                          \
                                                                               \
    TY* HEAP_PEEK(TY)(HEAP(TY) * this) {                                       \
        assert(this);                                                          \
        if (this->len == 0)                                                    \
            return NULL;                                                       \
        return this->data;                                                     \
    }
