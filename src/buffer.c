#include "buffer.h"

#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "ptr.h"
#include "ptr_util.h"
#include "util.h"

// TODO: This should probably hand out slices of a pre-registered buffer of some
// kind, to reduce the overhead of malloc and of mapping buffers into kernel
// memory. For now, it just allocates or initializes a buffer. A buffer can be
// overlaid on existing memory by passing it to buf_init with a non-null data
// field. In such cases, care should be taken not to trigger an unintended
// resize (and thus copy).
bool buf_init(Buffer* this, size_t cap, size_t max_cap) {
    if (!this->data.ptr)
        this->data = bstring_alloc(cap);
    TRYB(this->data.ptr);
    this->max_cap = max_cap;

    return true;
}

bool buf_initialized(const Buffer* this) {
    assert(this);
    assert(this->head <= this->tail);

    return this->data.ptr;
}

void buf_reset(Buffer* this) {
    assert(buf_initialized(this));

    this->head = 0;
    this->tail = 0;
}

bool buf_reset_if_empty(Buffer* this) {
    assert(buf_initialized(this));

    if (this->head != this->tail)
        return false;

    buf_reset(this);
    return true;
}

// Length of the contents of the buffer.
size_t buf_len(const Buffer* this) {
    assert(buf_initialized(this));
    return this->tail - this->head;
}

// Total available capacity for writing.
size_t buf_cap(const Buffer* this) {
    assert(buf_initialized(this));
    return this->data.len - buf_len(this);
}

// Available space for a single write (i.e., continguous space).
size_t buf_space(Buffer* this) {
    assert(buf_initialized(this));

    buf_reset_if_empty(this);
    return this->data.len - this->tail;
}

// Compact the contents to the start of the buffer.
static bool buf_compact(Buffer* this) {
    assert(buf_initialized(this));
    assert(this->head != 0);

    return memmove(this->data.ptr, &this->data.ptr[this->head], buf_len(this));
}

// Attempt to grow the buffer to fit at least min_extra_cap more bytes.
bool buf_ensure_cap(Buffer* this, size_t min_extra_cap) {
    assert(buf_initialized(this));

    if (buf_space(this) >= min_extra_cap)
        return true;
    // Nope.
    if (buf_len(this) + min_extra_cap > this->max_cap)
        return false;

    if (buf_cap(this) >= min_extra_cap)
        return buf_compact(this);

    size_t new_cap = this->data.len;
    for (; new_cap < this->data.len + min_extra_cap; new_cap *= 2)
        ;
    ByteString new_data =
        bstring_realloc(this->data, MIN(new_cap, this->max_cap));
    TRYB(new_data.ptr);
    this->data = new_data;

    return true;
}

// Attempt to grow the buffer to its maximum capacity.
bool buf_ensure_max_cap(Buffer* this) {
    assert(buf_initialized(this));

    if (this->data.len >= this->max_cap)
        return true;

    return buf_ensure_cap(this, this->max_cap - this->data.len);
}

// Pointer for writing into the buffer.
ByteString buf_write_ptr(Buffer* this) {
    assert(this);

    buf_reset_if_empty(this);
    return (ByteString){ .ptr = this->data.ptr + this->tail,
                         .len = buf_space(this) };
}

// Pointer for writing as a string into the buffer.
String buf_write_ptr_string(Buffer* this) {
    ByteString ret_bytes = buf_write_ptr(this);
    return (String){ .ptr = (char*)ret_bytes.ptr, .len = ret_bytes.len };
}

// Bytes have been written into the buffer.
void buf_wrote(Buffer* this, size_t len) {
    assert(buf_initialized(this));
    assert(this->tail + len <= this->data.len);

    this->tail += len;
}

bool buf_write_byte(Buffer* this, uint8_t byte) {
    assert(buf_initialized(this));

    TRYB(buf_ensure_cap(this, 1));

    this->data.ptr[this->tail++] = byte;

    return true;
}

bool buf_write_str(Buffer* this, CString str) {
    assert(buf_initialized(this));

    if (str.len + buf_len(this) > this->max_cap)
        return false;
    TRYB(buf_ensure_cap(this, str.len));

    bstring_copy(buf_write_ptr(this), cstring_as_cbstring(str));
    buf_wrote(this, str.len);
    return true;
}

bool buf_write_line(Buffer* this, CString str) {
    TRYB(buf_write_str(this, str));
    return buf_write_byte(this, '\n');
}

FORMAT_FN(2, 3)
bool buf_write_fmt(Buffer* this, const char* fmt, ...) {
    assert(buf_initialized(this));

    ByteString write_ptr = buf_write_ptr(this);

    va_list args;
    va_start(args, fmt);

    int rc = -1;
    if ((rc = vsnprintf((char*)write_ptr.ptr, write_ptr.len, fmt, args)) < 0)
        return false;
    va_end(args);

    buf_wrote(this, MIN(write_ptr.len, (size_t)rc));
    return true;
}

bool buf_write_num(Buffer* this, size_t num) {
    static char   _BUF[20] = { '\0' };
    static String BUF     = { .ptr = _BUF, .len = sizeof(_BUF) / sizeof(char) };
    String        num_str = string_itoa(BUF, num);
    return buf_write_str(this, S_CONST(num_str));
}

// Pointer for reading from the buffer.
CByteString buf_read_ptr(const Buffer* this) {
    return BS_CONST(buf_read_ptr_mut((Buffer*)this));
}

// Mutable pointer for reading from the buffer.
ByteString buf_read_ptr_mut(Buffer* this) {
    assert(buf_initialized(this));

    return (ByteString){ .ptr = this->data.ptr + this->head,
                         .len = buf_len(this) };
}

// Bytes have been consumed from the buffer.
void buf_read(Buffer* this, size_t len) {
    assert(buf_initialized(this));
    assert(this->head + len <= this->data.len);

    this->head += len;
    buf_reset_if_empty(this);
}

ByteString buf_memmem(Buffer* this, CString needle) {
    assert(buf_initialized(this));
    assert(needle.ptr);
    assert(needle.len > 0);

    if (buf_len(this) == 0)
        return BS_NULL;

    uint8_t* ret_ptr = memmem(&this->data.ptr[this->head], buf_len(this),
                              needle.ptr, needle.len);
    return (ByteString){ .ptr = ret_ptr, .len = needle.len };
}

// Get a token from the buffer. NOTE: This updates the head of the buffer, so
// care should be taken not to write into the buffer as long as the returned
// pointer is needed.
ByteString buf_token_next_impl(_buf_token_next_args args) {
    struct Buffer* this  = args.this;
    CString delim        = args.delim;
    bool    preserve_end = args.preserve_end;
    assert(buf_initialized(this));

    // <head>[delim][token][delim]...<tail>

    // Eat preceding delimiters.
    for (; this->head < this->tail &&
           strchr(delim.ptr, this->data.ptr[this->head]);
         buf_read(this, 1))
        ;

    // <head>[token][delim]...<tail>

    // Find following delimiter.
    size_t end = this->head;
    for (; end < this->tail && !strchr(delim.ptr, this->data.ptr[end]); end++)
        ;

    // Zero out all delimiters.
    size_t last = end;
    if (!preserve_end)
        for (; last < this->tail && strchr(delim.ptr, this->data.ptr[last]);
             last++)
            this->data.ptr[last] = '\0';

    ByteString ret = { .ptr = &this->data.ptr[this->head],
                       .len = end - this->head };
    this->head     = last;
    return ret;
}

bool buf_consume(Buffer* this, CString needle) {
    assert(this);
    assert(needle.ptr);

    ByteString pos = buf_memmem(this, needle);
    TRYB(pos.ptr);

    if ((size_t)(pos.ptr - this->data.ptr) != this->head)
        return false;

    buf_read(this, needle.len);
    return true;
}

void buf_free(Buffer* this) {
    assert(buf_initialized(this));

    bstring_free(&this->data);
    memset(this, 0, sizeof(Buffer));
}
