/*
 * URING -- io_uring bindings.
 *
 * Copyright (c) 2023-2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cassert>
#include <coroutine>
#include <expected>
#include <ostream>
#include <span>
#include <utility>

#include <liburing.h>

#include <a3/log.h>

#include "sc/lib/try.hh"

module sc.io.impl.uring;

import sc.io.alloc;
import sc.io.impl.uring.init;
import sc.io.impl.uring.sqe;
import sc.lib.error;

namespace sc::io::impl::uring {

namespace {

constexpr std::uint8_t FLAG_REGULAR_FILE{1};

}

Uring::Uring(unsigned entries) noexcept {
    init(m_uring, entries);
    buffer_flush();
}

Uring::~Uring() { ::io_uring_queue_exit(&m_uring); }

void Uring::buffer_flush() noexcept {
    Allocator::the().consume_free([this](Buf buf) { m_buffers.submit(std::move(buf)); });
}

::io_uring_sqe* Uring::sqe() noexcept {
    auto* s = ::io_uring_get_sqe(&m_uring);
    if (!s && ::io_uring_sq_ready(&m_uring) > 0) {
        lib::error::must(::io_uring_submit(&m_uring));
        s = ::io_uring_get_sqe(&m_uring);
    }

    return s;
}

void Uring::pump(bool wait) noexcept {
    buffer_flush();

    if (wait)
        lib::error::must(::io_uring_submit_and_wait(&m_uring, 1));
    else
        lib::error::must(::io_uring_submit(&m_uring));

    while (::io_uring_cq_ready(&m_uring)) {
        ::io_uring_cqe* cqe;
        lib::error::must(::io_uring_peek_cqe(&m_uring, &cqe));

        if (auto const data = ::io_uring_cqe_get_data64(cqe); data & IMMEDIATE_FLAG) {
            auto const fd = static_cast<unsigned>(data & ~IMMEDIATE_FLAG);
            A3_ERRNO_F(-cqe->res, "Failed to close file %u!", fd);
            ::io_uring_cqe_seen(&m_uring, cqe);
            continue;
        }

        auto& data = *static_cast<Cqe*>(::io_uring_cqe_get_data(cqe));
        data.complete(*cqe);

        ::io_uring_cqe_seen(&m_uring, cqe);
    }
}

template <template <typename> typename S>
auto Uring::prep(std::invocable<::io_uring_sqe&> auto f) {
    return S{[f = std::move(f), this]() mutable noexcept {
        auto* s = sqe();
        if (s) {
            s->buf_group = 0;
            std::invoke(std::move(f), *s);
        }

        return s;
    }};
}

template <template <typename> typename S>
auto Uring::prep(std::invocable<::io_uring_sqe*> auto f) {
    return prep<S>([f = std::move(f)](::io_uring_sqe& sqe) mutable noexcept {
        std::invoke(std::move(f), &sqe);
    });
}

bool Uring::prep_immediate(std::invocable<::io_uring_sqe&> auto f, std::uint64_t data) {
    auto* s = sqe();
    if (!s)
        return false;

    std::invoke(std::move(f), *s);
    s->flags |= IOSQE_CQE_SKIP_SUCCESS;
    ::io_uring_sqe_set_data64(s, data | IMMEDIATE_FLAG);
    return true;
}

co::Future<std::expected<void, Uring::Error>> Uring::nop() noexcept {
    co_return (co_await prep<Sqe>(::io_uring_prep_nop)).result().transform([](auto const&) {});
}

co::Generator<std::expected<Socket, Uring::Error>> Uring::accept(SocketRef socket) noexcept {
    while (true) {
        auto generator = prep<MultishotSqe>([socket](::io_uring_sqe& sqe) noexcept {
            ::io_uring_prep_multishot_accept_direct(&sqe, socket, nullptr, nullptr, 0);
        });

        while (!generator.done()) {
            auto const result = co_await generator;
            assert(result);

            co_yield result->result().transform([](std::uint32_t res) {
                return Socket{static_cast<Socket::Descriptor>(res), FLAG_REGULAR_FILE};
            });
        }
    }
}

co::Future<std::expected<Buf, Uring::Error>> Uring::recv(SocketRef socket) noexcept {
    auto const res = co_await prep<Sqe>([socket](::io_uring_sqe& sqe) noexcept {
        ::io_uring_prep_recv(&sqe, socket, nullptr, 0, 0);
        ::io_uring_sqe_set_flags(&sqe, IOSQE_BUFFER_SELECT | IOSQE_FIXED_FILE);
    });

    co_return res.result()
        .transform_error([](auto error) { return Uring::Error{error}; })
        .and_then([buf = res.m_buf](std::uint32_t len) -> std::expected<Buf, Uring::Error> {
            if (!buf)
                return std::unexpected{Errors::OutOfBuffers{}};

            return Allocator::the().get_unsafe(*buf).slice(len);
        });
}

co::Future<std::expected<void, Uring::Error>> Uring::send(SocketRef                  socket,
                                                          std::span<std::byte const> buf) noexcept {
    while (!buf.empty()) {
        auto const len =
            SC_CO_TRY((co_await prep<Sqe>([socket, buf](::io_uring_sqe& sqe) noexcept {
                          ::io_uring_prep_send(&sqe, socket, buf.data(), buf.size(), 0);
                          ::io_uring_sqe_set_flags(&sqe, IOSQE_FIXED_FILE);
                      })).result());

        assert(len <= buf.size());
        buf = buf.subspan(len);
    }

    co_return {};
}

co::Future<std::expected<Socket, Uring::Error>> Uring::socket() noexcept {
    co_return (co_await prep<Sqe>([](::io_uring_sqe& sqe) noexcept {
        ::io_uring_prep_socket(&sqe, AF_INET6, SOCK_STREAM, 0, 0);
    }))
        .result()
        .transform([&](std::uint32_t res) {
            return Socket{res, FLAG_REGULAR_FILE};
        });
}

std::expected<void, Uring::Errors::RingFull>
Uring::close(std::pair<File::Descriptor, std::uint8_t> file) noexcept {
    if (!prep_immediate(
            [file](::io_uring_sqe& sqe) noexcept {
                auto const [fd, flags] = file;

                ::io_uring_prep_close(&sqe, static_cast<int>(fd));

                if (!(flags & FLAG_REGULAR_FILE))
                    ::io_uring_sqe_set_flags(&sqe, IOSQE_FIXED_FILE);
            },
            file.first)) {
        return std::unexpected{Errors::RingFull{}};
    }

    return {};
}

std::ostream& operator<<(std::ostream& stream, Uring::Errors::RingFull) {
    return stream << "Submission queue full";
}

std::ostream& operator<<(std::ostream& stream, Uring::Errors::OutOfBuffers) {
    return stream << "Out of registered buffers";
}

} // namespace sc::io::impl::uring
