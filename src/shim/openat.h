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
 *
 * In the absence of a platform-provided mechanism for doing so, a slow fallback based on
 * /proc/self/fd is used.
 */

#pragma once

#include <stdint.h>

#include <a3/types.h>

#ifdef SC_HAVE_OPENAT2
#include <linux/openat2.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SC_RESOLVE_BENEATH RESOLVE_BENEATH
#else
#include <fcntl.h>

#define SC_RESOLVE_BENEATH 0x08
#endif

#if defined(SC_HAVE_OPENAT2) || defined(SC_HAVE_O_RESOLVE_BENEATH)
A3_ALWAYS_INLINE int sc_shim_openat(int dir, char const* path, uint64_t flags, uint64_t resolve) {
#ifdef SC_HAVE_OPENAT2
    return (int)syscall(SYS_openat2, dir, path,
                        &(struct open_how) { .flags = flags, .resolve = resolve },
                        sizeof(struct open_how));
#elif defined(SC_HAVE_O_RESOLVE_BENEATH)
    return openat(dir, path, flags | (resolve & SC_RESOLVE_BENEATH) ? O_RESOLVE_BENEATH : 0);
#endif
}
#else
int sc_shim_openat(int dir, char const* path, uint64_t flags, uint64_t resolve);
#endif
