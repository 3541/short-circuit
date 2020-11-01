#include "buffer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include "util.h"

// TODO: This should probably hand out slices of a pre-registered buffer of some
// kind, to reduce the overhead of malloc and of mapping buffers into kernel
// memory. For now, it just allocates a buffer.
bool buf_init(struct Buffer* this, size_t cap) {
    assert(!buf_initialized(this));

    this->data = calloc(cap, sizeof(uint8_t));
    if (!this->data)
        return false;
    this->cap = cap;

    return true;
}

bool buf_initialized(const struct Buffer* this) {
    assert(this);

    return this->data;
}

void buf_reset(struct Buffer* this) {
    assert(buf_initialized(this));

    this->head = 0;
    this->tail = 0;
}

// Length of the contents of the buffer.
size_t buf_len(const struct Buffer* this) {
    assert(buf_initialized(this));
    return (this->tail >= this->head) ? this->tail - this->head
                                      : this->cap - (this->head - this->tail);
}

// Length readable in a single read (i.e., contiguous data).
size_t buf_pending(const struct Buffer* this) {
    assert(buf_initialized(this));
    return (this->tail >= this->head) ? this->tail - this->head
                                      : this->cap - this->head;
}

// Total available capacity for writing.
size_t buf_cap(const struct Buffer* this) {
    assert(buf_initialized(this));
    return this->cap - buf_len(this) - 1;
}

// Available space for a single write (i.e., continguous space).
size_t buf_space(const struct Buffer* this) {
    assert(buf_initialized(this));
    return (this->tail >= this->head) ? this->cap - this->tail - !this->head
                                      : this->head - this->tail - 1;
}

// Pointer for writing into the buffer.
uint8_t* buf_write_ptr(struct Buffer* this) {
    assert(this);

    return this->data + this->tail;
}

// Bytes have been written into the buffer.
void buf_wrote(struct Buffer* this, size_t len) {
    assert(buf_initialized(this));
    assert(this->tail + len <= this->cap);

    this->tail += len;
    this->tail %= this->cap;
}

bool buf_write_byte(struct Buffer* this, uint8_t byte) {
    assert(buf_initialized(this));

    if (buf_space(this) < 1)
        return false;

    this->data[this->tail++] = byte;
    this->tail %= this->cap;

    return true;
}

bool buf_write_str(struct Buffer* this, const char* str) {
    assert(buf_initialized(this));

    size_t len = strnlen(str, buf_cap(this) + 1);
    if (len > buf_cap(this))
        PANIC("TODO: Buffer growing.");
    size_t i = 0;
    for (; i < len; i++)
        this->data[(this->tail + i) % this->cap] = str[i];

    buf_wrote(this, len);
    return true;
}

bool buf_write_line(struct Buffer* this, const char* str) {
    TRYB(buf_write_str(this, str));
    return buf_write_byte(this, '\n');
}

bool buf_write_fmt(struct Buffer* this, const char* fmt, ...) {
    assert(buf_initialized(this));

    size_t space = buf_space(this);

    va_list args;
    va_start(args, fmt);

    int rc = -1;
    if ((rc = vsnprintf((char*)buf_write_ptr(this), space, fmt, args)) < 0)
        return false;
    va_end(args);

    buf_wrote(this, MIN(space, (size_t)rc));
    return true;
}

bool buf_write_num(struct Buffer* this, ssize_t num) {
    return buf_write_fmt(this, "%zd", num);
}

// Pointer for reading from the buffer.
const uint8_t* buf_read_ptr(const struct Buffer* this) {
    return buf_read_ptr_mut((struct Buffer*)this);
}

// Mutable pointer for reading from the buffer.
uint8_t* buf_read_ptr_mut(struct Buffer* this) {
    assert(buf_initialized(this));

    return this->data + this->head;
}

// Bytes have been consumed from the buffer.
void buf_read(struct Buffer* this, size_t len) {
    assert(buf_initialized(this));
    assert(this->head + len <= this->cap);

    this->head += len;
    this->head %= this->cap;
}

// Similar to memmem, but handles wrapping of the buffer.
uint8_t* buf_memmem(struct Buffer* this, const char* needle) {
    assert(buf_initialized(this));
    assert(needle);

    if (buf_len(this) == 0)
        return NULL;

    size_t needle_len = strnlen(needle, buf_len(this));
    assert(needle_len > 0);

    uint8_t* found =
        memmem(&this->data[this->head], buf_pending(this), needle, needle_len);
    if (found)
        return found;

    // Not wrapped, so there's no needle.
    if (buf_len(this) == buf_pending(this))
        return NULL;
    return memmem(this->data, buf_len(this) - buf_pending(this), needle,
                  needle_len);
}

// Get a token from the buffer. NOTE: This updates the head of the buffer, so
// care should be taken not to write into the buffer as long as the returned
// pointer is needed.
uint8_t* buf_token_next(struct Buffer* this, const char* delim) {
    assert(buf_initialized(this));

    // <head>[delim][token][delim]...<tail>

    // Eat preceding delimiters.
    for (; this->head < this->tail && strchr(delim, this->data[this->head]);
         buf_read(this, 1))
        ;

    // <head>[token][delim]...<tail>

    // Find following delimiter.
    size_t end = this->head;
    for (; end < this->tail && !strchr(delim, this->data[end]);
         end = (end + 1) % this->cap)
        ;

    // Zero out all delimiters.
    size_t last = end;
    for (; last < this->tail && strchr(delim, this->data[last]);
         last = (last + 1) % this->cap)
        this->data[last] = '\0';

    // In this case, the buffer probably needs to be grown to fit the whole
    // token contiguously, and then the rest of the contents moved to the
    // beginning of the buffer.
    if (end < this->head)
        PANIC("TODO: Handle wrapping in buf_token_next.");

    uint8_t* ret = &this->data[this->head];
    this->head   = last;
    return ret;
}

bool buf_consume(struct Buffer* this, const char* needle) {
    assert(this);
    assert(needle);

    uint8_t* pos = buf_memmem(this, needle);
    TRYB(pos);

    if (pos - this->data != (ssize_t)this->head)
        return false;

    buf_read(this, strnlen(needle, buf_pending(this)));
    return true;
}

void buf_free(struct Buffer* this) {
    assert(buf_initialized(this));

    free(this->data);
    memset(this, 0, sizeof(struct Buffer));
}
