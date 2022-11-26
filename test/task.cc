#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sc/io/task.hh>
#include <sc/shim/coro.hh>

#include "mock_future.hh"

namespace sc::io::test {

using namespace testing;

TEST(Task, runs) {
    bool ran = false;

    [](bool& ran) -> Task {
        ran = true;
        co_return;
    }(ran);

    EXPECT_THAT(ran, IsTrue());
}

TEST(Task, await) {
    bool ran = false;

    [](bool& ran) -> Task {
        co_await costd::suspend_never{};
        ran = true;
    }(ran);

    EXPECT_THAT(ran, IsTrue());
}

TEST(Task, suspends) {
    bool ran            = false;
    bool passed_suspend = false;

    auto task = [](bool& ran, bool& passed_suspend) -> Task {
        ran = true;
        co_await costd::suspend_always{};
        passed_suspend = true;
    }(ran, passed_suspend);

    EXPECT_THAT(ran, IsTrue());
    EXPECT_THAT(passed_suspend, IsFalse());

    std::move(task).cancel();
}

TEST(Task, resumes) {
    MockFuture future;

    bool ran            = false;
    bool passed_suspend = false;

    [](bool& ran, bool& passed_suspend, MockFuture& future) -> Task {
        ran = true;
        co_await future;
        passed_suspend = true;
    }(ran, passed_suspend, future);

    EXPECT_THAT(ran, IsTrue());
    EXPECT_THAT(passed_suspend, IsFalse());
    EXPECT_THAT(future.m_handle, Not(IsNull()));

    future.resume();
    EXPECT_THAT(passed_suspend, IsTrue());
}

} // namespace sc::io::test
