#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Buffer {
    uint8_t* data;
    size_t   cap;
    size_t   len;
};

bool buf_init(struct Buffer*, size_t cap);
bool buf_initialized(struct Buffer*);

size_t buf_len(struct Buffer*);
size_t buf_cap(struct Buffer*);

uint8_t* buf_write_ptr(struct Buffer*);
void     buf_wrote(struct Buffer*, size_t);

void buf_free(struct Buffer*);
