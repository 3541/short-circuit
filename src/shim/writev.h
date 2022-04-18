/*
 * SHORT CIRCUIT: WRITEV SHIM -- Cross-platform definitions for writev.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 *
 * Works around different behavior on non-seekable files.
 */

#pragma once

#include <sys/types.h>
#include <sys/uio.h>

#include <a3/types.h>

A3_ALWAYS_INLINE ssize_t sc_shim_writev(int fd, struct iovec const* iov, int count, off_t offset) {
    if (offset < 0)
        return writev(fd, iov, count);
    return pwritev(fd, iov, count, offset);
}
