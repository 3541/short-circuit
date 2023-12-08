module;

#include <cassert>
#include <coroutine>
#include <expected>

#include <liburing.h>

module sc.io.impl.uring;

import sc.io.impl.uring.sqe;
import sc.io.alloc;
import sc.lib.error;

namespace sc::io::impl::uring {

Uring::Uring(unsigned entries) noexcept {
    lib::error::must(io_uring_queue_init(entries, &m_uring,
                                         IORING_SETUP_COOP_TASKRUN | IORING_SETUP_SINGLE_ISSUER));
    lib::error::must(io_uring_register_ring_fd(&m_uring));
}

co::Future<std::expected<void, Uring::Error>> Uring::nop() noexcept {
    co_return (co_await prep<Sqe>(io_uring_prep_nop)).result().transform([](auto const&) {});
}

co::Generator<std::expected<Socket, Uring::Error>> Uring::accept(SocketRef socket) noexcept {
    while (true) {
        auto generator = prep<MultishotSqe>([socket](io_uring_sqe& sqe) noexcept {
            io_uring_prep_multishot_accept_direct(&sqe, socket, nullptr, nullptr, 0);
        });

        while (!generator.done()) {
            auto const result = co_await generator;
            assert(result);

            co_yield result->result().transform(
                [](std::uint32_t res) { return Socket{static_cast<Socket::Descriptor>(res)}; });
        }
    }
}

co::Generator<std::expected<Buf, Uring::Error>> Uring::recv(SocketRef socket) noexcept {
    while (true) {
        auto generator = prep<MultishotSqe>([socket](io_uring_sqe& sqe) noexcept {
            sqe.flags |= IOSQE_BUFFER_SELECT;
            io_uring_prep_multishot_accept_direct(&sqe, socket, nullptr, nullptr, 0);
        });

        while (!generator.done()) {
            auto const result = co_await generator;
            assert(result);

            if (!result->m_buffer) {
                co_yield std::unexpected{Errors::OutOfBuffers{}};
                continue;
            }

            co_yield result->result().transform([id = *result->m_buffer](std::uint32_t res) {
                return Allocator::the().get_unsafe(id).slice(res);
            });
        }
    }
}

} // namespace sc::io::impl::uring
