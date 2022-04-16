#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/utsname.h>

#include <a3/log.h>

#include <sc/coroutine.h>
#include <sc/io.h>

using namespace testing;

class IoTest;
static ssize_t trampoline(ScCoroutine*, void*);

static thread_local IoTest* test = nullptr;

class IoTest : public Test {
private:
    ScEventLoop* loop;
    ScCoCtx*     main_ctx = sc_co_main_ctx_new();
    ScCoEntry    entry    = nullptr;
    ssize_t      result   = -1;

    friend ssize_t trampoline(ScCoroutine*, void*);

protected:
    long kver_major, kver_minor; // NOLINT(misc-non-private-member-variables-in-classes)

    IoTest() {
        a3_log_init(stderr, A3_LOG_WARN);
        loop = sc_io_event_loop_new();
        test = this;
    }

    void SetUp() override {
        utsname info;
        ASSERT_THAT(uname(&info), Ge(0));

        std::string release { info.release };
        char*       saveptr = NULL;

        kver_major = strtol(strtok_r(release.data(), ".", &saveptr), NULL, 10);
        kver_minor = strtol(strtok_r(NULL, ".", &saveptr), NULL, 10);

#ifdef SC_IO_BACKEND_URING
        if (kver_major < 5)
            GTEST_SKIP() << "io_uring not supported.";
#endif
    }

    ~IoTest() {
        sc_io_event_loop_free(loop);
        sc_co_main_ctx_free(main_ctx);
    }

    ssize_t run_on_coroutine(ScCoEntry f, void* data) {
        entry = f;

        auto* co = sc_co_new(main_ctx, loop, trampoline, data);
        sc_co_resume(co, 0);

        sc_io_event_loop_run(loop);

        return result;
    }

    static std::optional<std::pair<int, in_port_t>> serve_socket() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0)
            return {};

        int const enable = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0)
            return {};

        sockaddr_in addr {
            .sin_family = AF_INET,
            .sin_port   = 0,
            .sin_addr   = { .s_addr = inet_addr("127.0.0.1") },
            .sin_zero   = { 0 },
        };
        if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            return {};
        if (listen(sock, 32) < 0)
            return {};

        socklen_t addr_len = sizeof(addr);
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len) < 0)
            return {};

        return std::pair { sock, addr.sin_port };
    }
};

static ssize_t trampoline(ScCoroutine* self, void* data) {
    test->result = test->entry(self, data);
    return test->result;
}

TEST_F(IoTest, accept) {
#ifdef SC_IO_BACKEND_URING
    if (kver_major == 5 && kver_minor < 5)
        GTEST_SKIP();
#endif

    auto res = serve_socket();
    ASSERT_THAT(res.has_value(), IsTrue());
    auto [sock, port] = *res;

    std::thread client_thread(
        [](in_port_t port) {
            int         client = socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in server_addr {
                .sin_family = AF_INET,
                .sin_port   = port,
                .sin_addr   = { .s_addr = inet_addr("127.0.0.1") },
                .sin_zero   = { 0 },
            };

            (void)connect(client, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
            close(client);
        },
        port);

    int client_sock = static_cast<int>(run_on_coroutine(
        [](ScCoroutine* self, void* data) -> ssize_t {
            sockaddr_in peer_addr;
            socklen_t   addr_len = sizeof(peer_addr);
            return (ssize_t)sc_io_accept(self, *static_cast<int*>(data),
                                         reinterpret_cast<sockaddr*>(&peer_addr), &addr_len)
                .ok;
        },
        &sock));
    ASSERT_THAT(client_sock, Ge(0));
    ASSERT_THAT(close(client_sock), Ge(0));

    client_thread.join();
}

TEST_F(IoTest, openat) {
#ifdef SC_IO_BACKEND_URING
    if (kver_major == 5 && kver_minor < 6)
        GTEST_SKIP();
#endif

    int fd = static_cast<int>(run_on_coroutine(
        [](ScCoroutine* self, void* data) -> ssize_t {
            return (ssize_t)sc_io_open_under(
                       self, AT_FDCWD, a3_cstring_from(static_cast<char const*>(data)), O_RDONLY)
                .ok;
        },
        const_cast<char*>("build.ninja")));

    ASSERT_THAT(fd, Ge(0));

    std::vector<char> expected;
    std::ifstream     file { "build.ninja", std::ios::binary };
    std::copy(std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {},
              std::back_inserter(expected));

    std::vector<char> got;
    while (got.size() < expected.size()) {
        std::array<char, 2048> buf;

        ssize_t res = read(fd, buf.data(), buf.size());
        if (res <= 0)
            break;
        std::copy(buf.begin(), buf.begin() + res, std::back_inserter(got));
    }

    EXPECT_THAT(got, ContainerEq(expected));
}

TEST_F(IoTest, close) {
#ifdef SC_IO_BACKEND_URING
    if (kver_major == 5 && kver_minor < 6)
        GTEST_SKIP();
#endif

    int fd = open("build.ninja", O_RDONLY);
    ASSERT_THAT(fd, Ge(0));

    ssize_t res = run_on_coroutine(
        [](ScCoroutine* self, void* data) -> ssize_t {
            return SC_IO_IS_OK(sc_io_close(self, *static_cast<int*>(data))) ? 0 : -1;
        },
        &fd);
    EXPECT_THAT(res, Eq(0));
}

TEST_F(IoTest, recv) {
#ifdef SC_IO_BACKEND_URING
    if (kver_major == 5 && kver_minor < 6)
        GTEST_SKIP();
#endif
    EXPECT_THAT(signal(SIGPIPE, SIG_IGN), Ne(SIG_ERR));

    auto res = serve_socket();
    ASSERT_THAT(res.has_value(), IsTrue());
    auto [sock, port] = *res;

    int client = socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_THAT(client, Ge(0));
    sockaddr_in server_addr {
        .sin_family = AF_INET,
        .sin_port   = port,
        .sin_addr   = { .s_addr = inet_addr("127.0.0.1") },
        .sin_zero   = { 0 },
    };

    char        message[] = "Hello, world";
    std::thread server_thread(
        [](int sock, char* message, size_t len) {
            sockaddr_in addr;
            socklen_t   addr_len  = sizeof(addr);
            int         connected = accept(sock, reinterpret_cast<sockaddr*>(&addr), &addr_len);
            send(connected, message, len, 0);
            close(connected);
        },
        sock, message, sizeof(message));

    ASSERT_THAT(connect(client, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)),
                Ge(0));

    struct Data {
        A3String buf;
        int      fd;
    };

    A3String buf = a3_string_alloc(sizeof(message));
    Data     d { .buf = buf, .fd = client };
    ssize_t  ret = run_on_coroutine(
         [](ScCoroutine* self, void* data) {
            auto* d = static_cast<Data*>(data);
            return (ssize_t)sc_io_recv(self, d->fd, d->buf).ok;
         },
         &d);
    EXPECT_THAT(ret, Gt(0));
    EXPECT_THAT(reinterpret_cast<char*>(buf.ptr), StrEq(message));

    EXPECT_THAT(close(client), Ge(0));
    EXPECT_THAT(close(sock), Ge(0));

    server_thread.join();
    a3_string_free(&buf);
}

TEST_F(IoTest, read) {
    // No need to restrict kernel versions here, since io_read falls back to OP_READV when OP_READ
    // is not available.

    int fd = open("build.ninja", O_RDONLY);
    ASSERT_THAT(fd, Ge(0));

    struct Data {
        std::array<char, 2048>& buf;
        int                     fd;
        off_t                   off;
    };

    std::vector<char> expected;
    std::ifstream     file { "build.ninja", std::ios::binary };
    std::copy(std::istreambuf_iterator<char> { file }, std::istreambuf_iterator<char> {},
              std::back_inserter(expected));

    std::vector<char> got;
    while (got.size() < expected.size()) {
        std::array<char, 2048> buf;
        Data                   data { buf, fd, static_cast<off_t>(got.size()) };

        ssize_t res = run_on_coroutine(
            [](ScCoroutine* self, void* data) -> ssize_t {
                auto* d = static_cast<Data*>(data);
                return (ssize_t)sc_io_read(
                           self, d->fd,
                           a3_string_new(reinterpret_cast<uint8_t*>(d->buf.data()), d->buf.size()),
                           d->off)
                    .ok;
            },
            &data);
        if (res <= 0)
            break;
        std::copy(buf.begin(), buf.begin() + res, std::back_inserter(got));
    }

    EXPECT_THAT(got, ContainerEq(expected));
}
