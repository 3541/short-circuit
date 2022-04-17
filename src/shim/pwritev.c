/*
 * SHORT CIRCUIT: PWRITEV SHIM -- Cross-platform definitions for pwritev.
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

#include "pwritev.h"

#include <sys/uio.h>

ssize_t sc_shim_pwritev(int fd, struct iovec const* iov, int count, off_t offset) {
#ifdef SC_HAVE_PWRITEV2
    return pwritev2(fd, iov, count, offset, 0);
#else
    return pwritev(fd, iov, count, offset < 0 ? 0 : offset);
#endif
}
