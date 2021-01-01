#pragma once

#include <assert.h>
#include <stdint.h>

#include <a3/str.h>

#include "forward.h"

typedef struct FileHandle FileHandle;

void file_cache_init(void);
FileHandle* file_open(struct io_uring*, CString path, int32_t flags);
fd file_handle_fd(FileHandle*);
void file_close(FileHandle*, struct io_uring*);
void file_cache_destroy(struct io_uring*);
