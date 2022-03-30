#include <algorithm>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

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
    ssize_t run_on_coroutine(ScCoEntry f, void* data) {
        entry = f;

        auto* co = sc_co_new(main_ctx, loop, trampoline, data);
        sc_co_resume(co, 0);

        sc_io_event_loop_run(loop);

        return result;
    }

    IoTest() {
        a3_log_init(stderr, A3_LOG_WARN);
        loop = sc_io_event_loop_new();
        test = this;
    }

    ~IoTest() {
        sc_io_event_loop_free(loop);
        sc_co_main_ctx_free(main_ctx);
    }
};

static ssize_t trampoline(ScCoroutine* self, void* data) {
    test->result = test->entry(self, data);
    return test->result;
}

TEST_F(IoTest, openat) {
    int fd = static_cast<int>(run_on_coroutine(
        [](ScCoroutine* self, void* data) -> ssize_t {
            return sc_io_openat(self, AT_FDCWD, a3_cstring_from(static_cast<char const*>(data)),
                                O_RDONLY);
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
    int fd = open("build.ninja", O_RDONLY);
    ASSERT_THAT(fd, Ge(0));

    ssize_t res = run_on_coroutine(
        [](ScCoroutine* self, void* data) -> ssize_t {
            return sc_io_close(self, *static_cast<int*>(data));
        },
        &fd);
    EXPECT_THAT(res, Eq(0));
}

TEST_F(IoTest, read) {
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
                return sc_io_read(
                    self, d->fd,
                    a3_string_new(reinterpret_cast<uint8_t*>(d->buf.data()), d->buf.size()),
                    d->off);
            },
            &data);
        if (res <= 0)
            break;
        std::copy(buf.begin(), buf.begin() + res, std::back_inserter(got));
    }

    EXPECT_THAT(got, ContainerEq(expected));
}
