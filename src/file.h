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

typedef struct FileHandle FileHandle;

void        file_cache_init(void);
FileHandle* file_open(EventTarget*, struct io_uring*, A3CString path, int32_t flags);
FileHandle* file_openat(EventTarget*, struct io_uring*, FileHandle* dir, A3CString name,
                        int32_t flags);
fd          file_handle_fd(FileHandle*);
fd          file_handle_fd_unchecked(FileHandle*);
bool        file_handle_waiting(FileHandle*);
void        file_close(FileHandle*, struct io_uring*);
void        file_cache_destroy(struct io_uring*);
void        file_handle_event_handle(FileHandle*, struct io_uring*, int32_t status, bool chain);

A3_H_END
