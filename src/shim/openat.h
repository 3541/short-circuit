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
 *
 * This wrapper exists mostly to unify platform-dependent mechanisms by which to ensure that a path
 * does not escape a given directory, like RESOLVE_BENEATH on Linux.
 */

#pragma once

#include <stdint.h>

#include <a3/types.h>

#ifdef SC_HAVE_OPENAT2
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <unistd.h>
#else
#define RESOLVE_BENEATH 0x08
#endif

A3_ALWAYS_INLINE int sc_shim_openat(int dir, char const* path, uint64_t flags, uint64_t resolve) {
#ifdef SC_HAVE_OPENAT2
    return (int)syscall(SYS_openat2, dir, path,
                        &(struct open_how) { .flags = flags, .resolve = resolve },
                        sizeof(struct open_how));
#elif defined(SC_HAVE_O_RESOLVE_BENEATH)
    return openat(dir, path, flags | (resolve & RESOLVE_BENEATH) ? O_RESOLVE_BENEATH : 0);
#else
#error "No openat shim for this platform."
#endif
}
