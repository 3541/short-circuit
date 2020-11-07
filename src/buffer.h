#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "ptr.h"

// A growable buffer.
// tail: Index at which to write.
// head: Index from which to read.
// When head == tail, the buffer is empty. In such a condition,
// buf_reset_if_empty will reset both indices to 0.
typedef struct Buffer {
    ByteString data;
    size_t     tail;
    size_t     head;
    size_t     max_cap;
} Buffer;

bool buf_init(Buffer*, size_t cap, size_t max_cap);
bool buf_initialized(const Buffer*);
void buf_reset(Buffer*);
bool buf_reset_if_empty(Buffer*);

size_t buf_len(const Buffer*);
size_t buf_cap(const Buffer*);
size_t buf_space(Buffer*);

bool buf_ensure_cap(Buffer*, size_t extra_cap);
bool buf_ensure_max_cap(Buffer*);

ByteString buf_write_ptr(Buffer*);
String     buf_write_ptr_string(Buffer*);
void       buf_wrote(Buffer*, size_t);
bool       buf_write_byte(Buffer*, uint8_t);
bool       buf_write_str(Buffer*, CString);
bool       buf_write_line(Buffer*, CString);
bool       buf_write_fmt(Buffer*, const char* fmt, ...);
bool       buf_write_num(Buffer*, ssize_t);

CByteString buf_read_ptr(const Buffer*);
ByteString  buf_read_ptr_mut(Buffer*);
void        buf_read(Buffer*, size_t);
ByteString  buf_memmem(Buffer*, CString needle);
bool        buf_consume(Buffer*, CString needle);

// A hack for pseudo-optional arguments.
typedef struct _buf_token_next_args {
    Buffer* this;
    CString delim;
    bool    preserve_end;
} _buf_token_next_args;

ByteString buf_token_next_impl(_buf_token_next_args);

#define buf_token_next(BUF, DELIM, ...)                                        \
    buf_token_next_impl((_buf_token_next_args){                                \
        .this = (BUF), .delim = (DELIM), .preserve_end = false, __VA_ARGS__ })
#define buf_token_next_copy(BUF, DELIM, ...)                                   \
    bstring_clone(BS_CONST(buf_token_next((BUF), (DELIM), __VA_ARGS__)))
#define buf_token_next_str(BUF, DELIM, ...)                                    \
    bstring_as_string(buf_token_next((BUF), (DELIM), __VA_ARGS__))

void buf_free(Buffer*);
