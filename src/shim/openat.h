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

#ifdef SC_HAVE_OPENAT2
#include <linux/openat2.h>
#else
#define RESOLVE_BENEATH 0x08
#endif

int sc_shim_openat(int dir, char const* path, uint64_t flags, uint64_t resolve);
