/*
 * SHORT CIRCUIT: EVENT IO_URING BACKEND
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#include <span>

#include <liburing.h>

#include <a3/result.hh>
#include <a3/util.hh>

#include "event/io.hh"

namespace sc::ev {

class Uring {
    A3_PINNED(Uring);

private:
    class Sqe : public IOBase<Sqe, a3::Result<size_t, std::error_code>> {
    private:
        io_uring_sqe& m_sqe;

    public:
        explicit Sqe(io_uring_sqe& sqe) : m_sqe { sqe } {}

        void complete(ssize_t result) { IOBase::complete(a3::signed_result(result)); }

        void await_suspend(std::coroutine_handle<> caller) {
            io_uring_sqe_set_data(&m_sqe, this);
            IOBase::await_suspend(caller);
        }
    };

    io_uring m_uring;

public:
    Uring();
    ~Uring();

    Sqe read(int fd, std::span<std::byte> out, uint64_t offset = 0, uint32_t sqe_flags = 0);

    void pump();
};

using Backend = Uring;

} // namespace sc::ev
