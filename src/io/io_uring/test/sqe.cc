#include <gmock/gmock.h>

#include <coroutine>
#include <liburing.h>

import sc.io.impl.uring.sqe;

namespace sc::io::impl::uring::test {

using namespace testing;

struct SqeTest : public Test {
protected:
    struct MockSqe {
        MOCK_METHOD(io_uring_sqe*, operator(), (), (noexcept));
    };

    MockSqe m_mock;
    io_uring_sqe m_sqe;

    template <template <typename> typename S>
    auto victim() {
        return S{m_sqe}.operator co_await();
    }
};

TEST_F(SqeTest, single) {
    InSequence const sequence;

    auto victim = victim<Sqe>();
    EXPECT_THAT(victim.await_ready(), IsFalse());

    EXPECT_CALL(m_mock, operator()).WillOnce(Return(&m_sqe));
    std::noop_coroutine_handle const handle;
    victim.await_suspend(handle);

    io_uring_cqe cqe;
    cqe.res = 123;
    cqe.flags = IORING_CQE_F_BUFFER & (4 << IORING_CQE_BUFFER_SHIFT);
    victim.complete(cqe);

    auto const result = victim.await_resume();
    EXPECT_THAT(result.m_res, Eq(123));
    EXPECT_THAT(result.m_buffer, Eq(Buf::Id{4}));
}

TEST_F(SqeTest, multishot) {
    InSequence const sequence;

    auto victim = victim<MultishotSqe>();
    EXPECT_THAT(victim.await_ready(), IsFalse());

    EXPECT_CALL(m_mock, operator()).WillOnce(Return(&m_sqe));
    std::noop_coroutine_handle const handle;
    victim.await_suspend(handle);
    EXPECT_THAT(victim.done(), IsFalse());
    EXPECT_THAT(victim.await_resume().has_value(), IsFalse());

    io_uring_cqe cqe;
    cqe.res = 123;
    cqe.flags = IORING_CQE_F_MORE;
    victim.complete(cqe);
    EXPECT_THAT(victim.done(), IsFalse());

    auto result = victim.await_resume();
    EXPECT_THAT(victim.done(), IsFalse());
    EXPECT_THAT(result, Eq(Result{123, {}}));

    victim.complete(cqe);
    cqe.flags = 0;
    victim.complete(cqe);
    EXPECT_THAT(victim.done(), IsFalse());

    EXPECT_THAT(victim.await_resume().has_value(), IsTrue());
    EXPECT_THAT(victim.await_resume().has_value(), IsTrue());
    EXPECT_THAT(victim.done(), IsTrue());
}

}
