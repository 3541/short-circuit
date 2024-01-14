#include <chrono>
#include <coroutine>
#include <expected>
#include <thread>

#include <fcntl.h>
#include <gmock/gmock.h>
#include <netinet/in.h>

#include "sc/lib/defer.hh"
#include "sc/lib/try.hh"

import sc.co.future;
import sc.io.addr;
import sc.io.file;
import sc.io.interface;
import sc.lib.error;
import sc.lib.expected;
import sc.lib.stream;
import sc.lib.tuple;
import sc.lib.variant;

namespace sc::io::test {

using namespace std::chrono_literals;

namespace {

const Port PORT{3541};

}

using namespace testing;

struct IoTest : public Test {
protected:
    Interface& m_victim{Interface::the()};

    std::expected<std::pair<Socket, int>, lib::WithContext<Interface::Error>> socket_pair();
};

std::expected<std::pair<Socket, int>, lib::WithContext<Interface::Error>> IoTest::socket_pair() {
    auto const lsock = SC_TRY(m_victim.run(m_victim.listen_socket(Addr::any(PORT))),
                              "Failed to create listening socket");

    auto const csock = static_cast<int>(lib::error::must(::socket(AF_INET6, SOCK_STREAM, 0)));

    std::thread thread{[&] {
        sockaddr_in6 const addr{
            .sin6_family = AF_INET6, .sin6_port = PORT, .sin6_addr = IN6ADDR_LOOPBACK_INIT};

        bool success = false;
        for (std::size_t attempts = 0; attempts < 10; ++attempts) {
            if (::connect(csock, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) == 0) {
                success = true;
                break;
            }

            std::this_thread::sleep_for(10ms);
        }

        EXPECT_THAT(success, IsTrue()) << "Failed to connect after 10 attempts: " << errno;
    }};

    auto asock = SC_TRY(m_victim.run([&]() -> co::Future<std::expected<Socket, Interface::Error>> {
        co_return co_await m_victim.accept(lsock);
    }()),
                        "Failed to accept");

    thread.join();

    return std::pair{std::move(asock), csock};
}

TEST_F(IoTest, nop) {
    auto const result = m_victim.run(m_victim.nop());

    EXPECT_THAT(result, IsTrue());
}

TEST_F(IoTest, socket_open_close) {
    int fd = -1;
    {
        auto const socket = m_victim.run(m_victim.listen_socket());

        ASSERT_THAT(socket, IsTrue()) << "Failed to create socket: " << lib::stream_view(socket);
        fd = *socket;
    }

    m_victim.pump(false);
    EXPECT_THAT(::fcntl(fd, F_GETFD), Lt(0));
    EXPECT_THAT(errno, Eq(EBADF));
}

TEST_F(IoTest, socket_accept) {
    auto [_, fd] = lib::error::must(socket_pair(), "Failed to create socket pair");
    lib::error::must(::close(fd));
    m_victim.pump(false);
}

TEST_F(IoTest, socket_recv) {
    auto [ssock, cfd] = lib::error::must(socket_pair(), "Failed to create socket pair");
    SC_DEFER {
        lib::error::must(::close(cfd));
        m_victim.pump(false);
    };

    constexpr std::string_view MSG{"Testing"};

    lib::error::must(::send(cfd, MSG.data(), MSG.size(), 0));
    auto const res = m_victim.run(m_victim.recv(ssock));

    ASSERT_THAT(res, IsTrue()) << "Failed to read from socket: " << lib::stream_view(res);
    EXPECT_THAT(res->as_str(), Eq(MSG));
}

TEST_F(IoTest, socket_send) {
    auto [ssock, cfd] = lib::error::must(socket_pair(), "Failed to create socket pair");
    SC_DEFER {
        lib::error::must(::close(cfd));
        m_victim.pump(false);
    };

    constexpr std::string_view MSG{"Another test"};

    auto const res = m_victim.run(m_victim.send(
        ssock, std::span{reinterpret_cast<std::byte const*>(MSG.data()), MSG.size()}));
    ASSERT_THAT(res, IsTrue()) << "Failed to send to socket: " << lib::stream_view(res);

    std::array<char, MSG.size()> buf{'\0'};
    auto const                   len = lib::error::must(::recv(cfd, buf.data(), buf.size(), 0));

    EXPECT_THAT((std::string_view{buf.data(), len}), Eq(MSG));
}

} // namespace sc::io::test
