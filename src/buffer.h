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
struct Buffer {
    ByteString data;
    size_t     tail;
    size_t     head;
    size_t     max_cap;
};

bool buf_init(struct Buffer*, size_t cap, size_t max_cap);
bool buf_initialized(const struct Buffer*);
void buf_reset(struct Buffer*);
bool buf_reset_if_empty(struct Buffer*);

size_t buf_len(const struct Buffer*);
size_t buf_cap(const struct Buffer*);
size_t buf_space(struct Buffer*);

bool buf_ensure_cap(struct Buffer*, size_t extra_cap);

ByteString buf_write_ptr(struct Buffer*);
String buf_write_ptr_string(struct Buffer*);
void       buf_wrote(struct Buffer*, size_t);
bool       buf_write_byte(struct Buffer*, uint8_t);
bool       buf_write_str(struct Buffer*, CString);
bool       buf_write_line(struct Buffer*, CString);
bool       buf_write_fmt(struct Buffer*, const char* fmt, ...);
bool       buf_write_num(struct Buffer*, ssize_t);

CByteString buf_read_ptr(const struct Buffer*);
ByteString  buf_read_ptr_mut(struct Buffer*);
void        buf_read(struct Buffer*, size_t);
ByteString  buf_memmem(struct Buffer*, CString needle);
ByteString  buf_token_next(struct Buffer*, CString delim);
ByteString  buf_token_next_copy(struct Buffer*, CString delim);
String      buf_token_next_str(struct Buffer*, CString delim);
bool        buf_consume(struct Buffer*, CString needle);

void buf_free(struct Buffer*);
