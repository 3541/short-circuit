/*
 * SHORT CIRCUIT: OPENAT SHIM -- Cross-platform openat wrapper.
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
 */

#if !defined(SC_HAVE_OPENAT2) && !defined(SC_HAVE_O_RESOLVE_BENEATH)

#include "openat.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <unistd.h>

#include <a3/str.h>
#include <a3/util.h>

#define PROC_SELF_PATH "/proc/self/fd/"

static A3CString sc_file_path(int file, char out[static PATH_MAX]) {
    if (file == AT_FDCWD) {
        if (!getcwd(out, PATH_MAX))
            return A3_CS_NULL;
        return a3_cstring_from(out);
    }

    char self_path[sizeof(PROC_SELF_PATH) + 16] = PROC_SELF_PATH;
    a3_string_itoa_into(a3_string_new((uint8_t*)&self_path[sizeof(PROC_SELF_PATH) - 1],
                                      sizeof(self_path) - sizeof(PROC_SELF_PATH) + 1),
                        (size_t)file);

    ssize_t res = readlink(self_path, out, PATH_MAX);
    if (res < 0)
        return A3_CS_NULL;
    return a3_cstring_new((uint8_t*)out, (size_t)res);
}

static bool sc_file_is_under(int dir, int file) {
    char dir_path_buf[PATH_MAX]  = { '\0' };
    char file_path_buf[PATH_MAX] = { '\0' };

    A3CString dir_path = sc_file_path(dir, dir_path_buf);
    if (!dir_path.ptr)
        return false;
    A3CString file_path = sc_file_path(file, file_path_buf);
    if (!file_path.ptr)
        return false;

    for (size_t i = 0; i < dir_path.len; i++) {
        if (dir_path.ptr[i] != file_path.ptr[i])
            return false;
    }

    return true;
}

int sc_shim_openat(int dir, char const* path, uint64_t flags, uint64_t resolve) {
    assert(flags <= INT_MAX);

    int res = openat(dir, path, (int)flags);

    if (res < 0)
        return res;

    if ((resolve & RESOLVE_BENEATH) && !sc_file_is_under(dir, res)) {
        A3_UNWRAPSD(close(res));
        errno = EXDEV;
        return -1;
    }

    return res;
}

#endif
