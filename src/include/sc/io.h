/*
 * SHORT CIRCUIT: IO -- Primitive IO operations.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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
 *
 * Formerly known as event.h.
 */

#pragma once

#include <netinet/in.h>
#include <stdbool.h>

#include <a3/cpp.h>
#include <a3/macro.h>
#include <a3/str.h>
#include <a3/types.h>

#include <sc/forward.h>

A3_H_BEGIN

#define SC_IO_ERROR_ENUM                                                                           \
    ERR(SC_IO_SUBMIT_FAILED, (-1), "IO event submission failed.")                                  \
    ERR(SC_IO_FILE_NOT_FOUND, (-2), "Target file not found.")                                      \
    ERR(SC_IO_SOCKET_CLOSED, (-3), "Connection closed by peer.")

typedef enum ScIoError {
#define ERR(N, V, S) N = (V),
    SC_IO_ERROR_ENUM
#undef ERR
} ScIoError;

#define SC_IO_RESULT(T) A3_M_PASTE(ScIoResult, T)
#define SC_DEFINE_IO_RESULT(T)                                                                     \
    typedef union SC_IO_RESULT(T) {                                                                \
        T         ok;                                                                              \
        ScIoError err;                                                                             \
    } SC_IO_RESULT(T)

SC_DEFINE_IO_RESULT(size_t);
SC_DEFINE_IO_RESULT(ScFd);
SC_DEFINE_IO_RESULT(bool);

#define SC_IO_OK(T, O)  ((SC_IO_RESULT(T)) { .ok = (O) })
#define SC_IO_ERR(T, E) ((SC_IO_RESULT(T)) { .err = (E) })

#define SC_IO_IS_ERR(R)                                                                            \
    ({                                                                                             \
        __typeof__(R) _res = (R);                                                                  \
        _res.err < 0;                                                                              \
    })
#define SC_IO_IS_OK(R) (!SC_IO_IS_ERR(R))

#define SC_IO_TRY(T, R)                                                                            \
    ({                                                                                             \
        __typeof__(R) _try_res = (R);                                                              \
        if (SC_IO_IS_ERR(_try_res)) {                                                              \
            SC_IO_RESULT(T) _ret;                                                                  \
            _ret.err = _try_res.err;                                                               \
            return _ret;                                                                           \
        }                                                                                          \
        _try_res.ok;                                                                               \
    })

#define SC_IO_TRY_MAP(R, V)                                                                        \
    ({                                                                                             \
        __typeof__(R) _try_res = (R);                                                              \
        if (SC_IO_IS_ERR(_try_res))                                                                \
            return (V);                                                                            \
        _try_res.ok;                                                                               \
    })

#define SC_IO_UNWRAP(R)                                                                            \
    ({                                                                                             \
        __typeof__(R) _unwrap_res = (R);                                                           \
        if (SC_IO_IS_ERR(_unwrap_res))                                                             \
            A3_PANIC_FMT("UNWRAP: " A3_S_F, A3_S_FORMAT(sc_io_error_to_string(_unwrap_res.err)));  \
        _unwrap_res.ok;                                                                            \
    })

struct statx;

A3_EXPORT A3CString sc_io_error_to_string(ScIoError);

A3_EXPORT ScEventLoop* sc_io_event_loop_new(void);
A3_EXPORT void         sc_io_event_loop_run(ScCoMain*);
A3_EXPORT void         sc_io_event_loop_pump(ScEventLoop*);
A3_EXPORT void         sc_io_event_loop_free(ScEventLoop*);

A3_EXPORT SC_IO_RESULT(ScFd)
    sc_io_accept(ScCoroutine*, ScFd sock, struct sockaddr* client_addr, socklen_t* addr_len);
A3_EXPORT SC_IO_RESULT(ScFd)
    sc_io_open_under(ScCoroutine*, ScFd dir, A3CString path, uint64_t flags);
A3_EXPORT SC_IO_RESULT(bool) sc_io_close(ScCoroutine*, ScFd);
A3_EXPORT SC_IO_RESULT(size_t) sc_io_recv(ScCoroutine*, ScFd sock, A3String dst);
A3_EXPORT SC_IO_RESULT(size_t) sc_io_read(ScCoroutine*, ScFd, A3String dst, off_t);
A3_EXPORT SC_IO_RESULT(size_t)
    sc_io_writev(ScCoroutine*, ScFd, struct iovec const*, unsigned count, off_t);
A3_EXPORT SC_IO_RESULT(bool) sc_io_stat(ScCoroutine*, ScFd file, struct statx*, unsigned mask);

A3_H_END
