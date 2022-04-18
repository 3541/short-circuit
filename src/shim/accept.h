/*
 * SHORT CIRCUIT: ACCEPT SHIM -- Cross-platform accept wrapper.
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
 * Ensures SOCK_NONBLOCK functionality.
 */

#pragma once

#include <fcntl.h>
#include <sys/socket.h>

#include <a3/types.h>

#ifdef SC_HAVE_SOCK_NONBLOCK
#define SC_SOCK_NONBLOCK SOCK_NONBLOCK
#else
#define SC_SOCK_NONBLOCK 00004000
#endif

A3_ALWAYS_INLINE int sc_shim_accept(int sock, struct sockaddr* addr, socklen_t* addr_len,
                                    int flags) {
#ifdef SC_HAVE_ACCEPT4
    return accept4(sock, addr, addr_len, flags);
#else
    int res = accept(sock, addr, addr_len);
    if (res < 0)
        return res;

    if (flags & SC_SOCK_NONBLOCK) {
        int old_flags = fcntl(sock, F_GETFL, 0);
        A3_UNWRAPSD(old_flags);
        A3_UNWRAPSD(fcntl(sock, F_SETFL, old_flags | O_NONBLOCK));
    }

    return res;
#endif
}
