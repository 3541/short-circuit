/*
 * SHORT CIRCUIT: FILE -- Open file descriptor cache.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <a3/cpp.h>
#include <a3/str.h>

#include "forward.h"

A3_H_BEGIN

struct statx;

typedef struct FileHandle FileHandle;
typedef void (*FileHandleHandler)(EventTarget* target, struct io_uring*, void* ctx, bool success,
                                  int32_t status);

#define FILE_STATX_MASK (STATX_TYPE | STATX_MTIME | STATX_INO | STATX_SIZE)

void        file_cache_init(void);
FileHandle* file_open(EventTarget*, struct io_uring*, FileHandleHandler, void* ctx, A3CString path,
                      int32_t flags);
FileHandle* file_openat(EventTarget*, struct io_uring*, FileHandleHandler, void* ctx,
                        FileHandle* dir, A3CString name, int32_t flags);
fd          file_handle_fd(FileHandle*);
fd          file_handle_fd_unchecked(FileHandle*);
struct statx* file_handle_stat(FileHandle*);
A3CString     file_handle_path(FileHandle*);
bool          file_handle_waiting(FileHandle*);
bool          file_handle_close(FileHandle*, struct io_uring*);
void          file_cache_destroy(struct io_uring*);

A3_H_END
