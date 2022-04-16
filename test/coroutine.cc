#include <gtest/gtest.h>

#include <a3/log.h>

#include <sc/coroutine.h>

#include "sc/forward.h"

using namespace testing;

class CoroutineTest : public Test {
protected:
    // NOLINTNEXTLINE(misc-non-private-member-variables-in-classes)
    ScCoCtx* main_ctx;

    CoroutineTest() {
        a3_log_init(stderr, A3_LOG_WARN);
        main_ctx = sc_co_main_ctx_new();
    }
    ~CoroutineTest() { sc_co_main_ctx_free(main_ctx); }
};

TEST_F(CoroutineTest, construction) {
    bool  ran = false;
    auto* co  = sc_co_new(
         main_ctx, nullptr,
         [](ScCoroutine* self, void* data) -> ssize_t {
            (void)self;
            *static_cast<bool*>(data) = true;
            return 42;
         },
         &ran);

    EXPECT_EQ(sc_co_resume(co, 0), 42);
    EXPECT_TRUE(ran);
}

TEST_F(CoroutineTest, spawn) {
    bool ran  = false;
    bool ran1 = false;

    struct Data {
        bool& ran;
        bool& ran1;
    };

    Data d { ran, ran1 };

    auto* co = sc_co_new(
        main_ctx, nullptr,
        [](ScCoroutine* self, void* data) -> ssize_t {
            auto* d   = static_cast<Data*>(data);
            auto* co1 = sc_co_spawn(
                self,
                [](ScCoroutine* self, void* data) -> ssize_t {
                    (void)self;
                    *static_cast<bool*>(data) = true;
                    return 42;
                },
                &d->ran1);
            auto res = sc_co_resume(co1, 0);
            d->ran   = true;
            return res;
        },
        &d);

    EXPECT_EQ(sc_co_resume(co, 0), 42);
    EXPECT_TRUE(ran);
    EXPECT_TRUE(ran1);
}

TEST_F(CoroutineTest, yield) {
    auto* co = sc_co_new(
        main_ctx, nullptr,
        [](ScCoroutine* self, void* data) -> ssize_t {
            (void)data;

            ssize_t val = 0;
            ssize_t res = 0;
            while ((val = sc_co_yield(self)))
                res += val;

            return res;
        },
        nullptr);

    size_t sum = 0;
    for (size_t i = 0; i < 100; i++) {
        sum += i;
        sc_co_resume(co, static_cast<ssize_t>(i));
    }

    EXPECT_EQ(static_cast<size_t>(sc_co_resume(co, 0)), sum);
}

TEST_F(CoroutineTest, defer) {
    bool defer_ran = false;

    auto* co = sc_co_new(
        main_ctx, nullptr,
        [](ScCoroutine* self, void* data) -> ssize_t {
            sc_co_defer(
                self, [](void* data) { *static_cast<bool*>(data) = true; }, data);

            return 0;
        },
        &defer_ran);

    EXPECT_EQ(sc_co_resume(co, 0), 0);
    EXPECT_TRUE(defer_ran);
}

TEST_F(CoroutineTest, defer_many_yield) {
    bool defer_ran = false;

    auto* co = sc_co_new(
        main_ctx, nullptr,
        [](ScCoroutine* self, void* data) -> ssize_t {
            sc_co_defer(
                self, [](void* data) { *static_cast<bool*>(data) = true; }, data);

            while (sc_co_yield(self))
                ;

            return 0;
        },
        &defer_ran);

    for (size_t i = 0; i < 100; i++) {
        EXPECT_EQ(sc_co_resume(co, 1), 1);
        EXPECT_FALSE(defer_ran);
    }

    EXPECT_EQ(sc_co_resume(co, 0), 0);
    EXPECT_TRUE(defer_ran);
}

TEST_F(CoroutineTest, many_coroutines) {
    std::vector<ScCoroutine*> coroutines;
    size_t                    n = 0;
    std::generate_n(std::back_inserter(coroutines), 400, [this, &n] {
        return sc_co_new(
            main_ctx, nullptr,
            [](ScCoroutine* self, void* data) -> ssize_t {
                ssize_t val = 0;
                ssize_t res = static_cast<ssize_t>(reinterpret_cast<uintptr_t>(data));

                while ((val = sc_co_yield(self)))
                    res += val;

                return res;
            },
            reinterpret_cast<void*>(n++));
    });

    size_t sum = 0;
    for (size_t i = 0; i < 100; i++) {
        sum += i;
        for (auto* co : coroutines)
            sc_co_resume(co, static_cast<ssize_t>(i));
    }

    n = 0;
    for (auto* co : coroutines)
        EXPECT_EQ(static_cast<size_t>(sc_co_resume(co, 0)), sum + n++);
}
