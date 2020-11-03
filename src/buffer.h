#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

// A growable buffer.
// tail: Index at which to write.
// head: Index from which to read.
// When head == tail, the buffer is empty. In such a condition,
// buf_reset_if_empty will reset both indices to 0.
struct Buffer {
    uint8_t* data;
    size_t   tail;
    size_t   head;
    size_t   cap;
    size_t max_cap;
};

bool buf_init(struct Buffer*, size_t cap, size_t max_cap);
bool buf_initialized(const struct Buffer*);
void buf_reset(struct Buffer*);
bool buf_reset_if_empty(struct Buffer*);

size_t buf_len(const struct Buffer*);
size_t buf_cap(const struct Buffer*);
size_t buf_space(struct Buffer*);

bool buf_ensure_cap(struct Buffer*, size_t extra_cap);

uint8_t* buf_write_ptr(struct Buffer*);
void     buf_wrote(struct Buffer*, size_t);
bool     buf_write_byte(struct Buffer*, uint8_t);
bool     buf_write_str(struct Buffer*, const char*);
bool     buf_write_line(struct Buffer*, const char*);
bool     buf_write_fmt(struct Buffer*, const char* fmt, ...);
bool     buf_write_num(struct Buffer*, ssize_t);

const uint8_t* buf_read_ptr(const struct Buffer*);
uint8_t*       buf_read_ptr_mut(struct Buffer*);
void           buf_read(struct Buffer*, size_t);
uint8_t*       buf_memmem(struct Buffer*, const char* needle);
uint8_t*       buf_token_next(struct Buffer*, const char* delim);
bool           buf_consume(struct Buffer*, const char* needle);

void buf_free(struct Buffer*);
