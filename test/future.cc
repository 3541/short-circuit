#include <type_traits>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sc/io/future.hh>
#include <sc/io/task.hh>
#include <sc/shim/coro.hh>

#include "mock_future.hh"

namespace sc::io::test {

using namespace testing;

template <typename FF, typename... Args>
using FResult = decltype(typename std::invoke_result_t<FF, Args...>::promise_type{}.result());

template <typename F, typename... Args, typename Result = FResult<F, Args...>>
std::enable_if_t<!std::is_void_v<Result>, Result> run(F&& fn, Args&&... args) {
    FResult<F, Args...> result;

    [](FResult<F, Args...>& result, auto&& fn, auto&&... args) -> Task {
        result = co_await std::forward<decltype(fn)>(fn)(std::forward<decltype(args)>(args)...);
    }(result, std::forward<F>(fn), std::forward<Args>(args)...);

    return result;
}

template <typename F, typename... Args>
std::enable_if_t<std::is_void_v<FResult<F, Args...>>> run(F&& fn, Args&&... args) {
    [](auto&& fn, auto&&... args) -> Task {
        co_await std::forward<decltype(fn)>(fn)(std::forward<decltype(args)>(args)...);
    }(std::forward<F>(fn), std::forward<Args>(args)...);
}

TEST(Future, runs) {
    bool ran = false;

    run(
        [](bool& ran) -> Future<> {
            ran = true;
            co_return;
        },
        ran);

    EXPECT_THAT(ran, IsTrue());
}

TEST(Future, returns_simple_value) {
    auto result = run([](int a, int b) -> Future<int> { co_return a + b; }, 5, 6);

    EXPECT_THAT(result, Eq(11));
}

TEST(Future, suspends_and_resumes) {
    MockFuture future;

    bool ran            = false;
    bool passed_suspend = false;

    run(
        [](bool& ran, bool& passed_suspend, MockFuture& future) -> Future<> {
            ran = true;
            co_await future;
            passed_suspend = true;
        },
        ran, passed_suspend, future);

    EXPECT_THAT(ran, IsTrue());
    EXPECT_THAT(passed_suspend, IsFalse());

    future.resume();

    EXPECT_THAT(passed_suspend, IsTrue());
}

} // namespace sc::io::test
