/*
 * FILE HANDLE -- The file handle type. See file.h and file.c
 *
 * Copyright (c) 2021, Alex O'Brien <3541ax@gmail.com>
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

#include <a3/sll.h>
#include <a3/str.h>

#include "event.h"
#include "forward.h"

typedef struct FileHandle {
    EVENT_TARGET;
    EventQueue waiting;

    A3CString path;
    fd        file;
    uint32_t  open_count;
    int32_t   flags;
} FileHandle;
