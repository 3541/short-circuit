#include "buffer.h"

#include <assert.h>
#include <stdlib.h>

// TODO: This should probably hand out slices of a pre-registered buffer of some
// kind, to reduce the overhead of malloc and of mapping buffers into kernel
// memory. For now, it just allocates a buffer.
bool buf_init(struct Buffer* this, size_t cap) {
    assert(this);
    assert(!buf_initialized(this));

    this->data = calloc(cap, sizeof(uint8_t));
    if (!this->data)
        return false;
    this->cap = cap;

    return true;
}

bool buf_initialized(struct Buffer* this) {
    assert(this);

    return this->data;
}

// Length of the contents of the buffer.
size_t buf_len(struct Buffer* this) {
    assert(this);
    assert(buf_initialized(this));
    return this->len;
}

// Available capacity for writing.
size_t buf_cap(struct Buffer* this) {
    assert(buf_initialized(this));
    return this->cap - this->len;
}

uint8_t* buf_write_ptr(struct Buffer* this) {
    assert(this);

    return this->data + this->len;
}

// Bytes have been written into the buffer.
void buf_wrote(struct Buffer* this, size_t len) {
    assert(this);
    assert(buf_initialized(this));
    assert(this->len + len <= this->cap);

    this->len += len;
}

void buf_free(struct Buffer* this) {
    assert(this);
    assert(buf_initialized(this));

    free(this->data);
    this->data = NULL;
    this->cap = 0;
    this->len = 0;
}
