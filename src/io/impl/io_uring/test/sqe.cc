#include <coroutine>

#include <gmock/gmock.h>
#include <liburing.h>

import sc.io.buf;
import sc.io.impl.uring.sqe;

namespace sc::io::impl::uring::test {

using namespace testing;

struct SqeTest : public Test {
protected:
    struct MockSqe {
        MOCK_METHOD(io_uring_sqe*, invoke, (), (noexcept));

        io_uring_sqe* operator()() noexcept { return invoke(); }
    };

    MockSqe      m_mock;
    io_uring_sqe m_sqe;

    template <template <typename> typename S>
    auto makeVictim() {
        return S<std::reference_wrapper<MockSqe>>{m_mock};
    }
};

TEST_F(SqeTest, single) {
    InSequence const sequence;

    EXPECT_CALL(m_mock, invoke()).WillOnce(Return(&m_sqe));
    auto victim = makeVictim<Sqe>().operator co_await();
    EXPECT_THAT(victim.await_ready(), IsFalse());

    auto const handle = std::noop_coroutine();
    victim.await_suspend(handle);

    io_uring_cqe cqe;
    cqe.res   = 123;
    cqe.flags = IORING_CQE_F_BUFFER | (4 << IORING_CQE_BUFFER_SHIFT);
    victim.complete(cqe);

    auto const result = victim.await_resume();
    EXPECT_THAT(result.m_res, Eq(123));
    EXPECT_THAT(result.m_buf, Eq(Buf::Id{4}));
}

TEST_F(SqeTest, multishot) {
    InSequence const sequence;

    auto victim = makeVictim<MultishotSqe>();
    EXPECT_THAT(victim.await_ready(), IsFalse());

    EXPECT_CALL(m_mock, invoke()).WillOnce(Return(&m_sqe));
    auto const handle = std::noop_coroutine();
    victim.await_suspend(handle);
    EXPECT_THAT(victim.done(), IsFalse());
    EXPECT_THAT(victim.await_resume().has_value(), IsFalse());

    io_uring_cqe cqe;
    cqe.res   = 123;
    cqe.flags = IORING_CQE_F_MORE;
    victim.complete(cqe);
    EXPECT_THAT(victim.done(), IsFalse());

    auto result = victim.await_resume();
    EXPECT_THAT(victim.done(), IsFalse());
    EXPECT_THAT(result.transform([](auto const& result) { return result.m_res; }), Eq(123));
    EXPECT_THAT(result.and_then([](auto const& result) { return result.m_buf; }), Eq(std::nullopt));
    EXPECT_THAT(victim.await_ready(), IsFalse());

    victim.complete(cqe);
    EXPECT_THAT(victim.await_ready(), IsTrue());
    cqe.flags = 0;
    victim.complete(cqe);
    EXPECT_THAT(victim.done(), IsFalse());

    EXPECT_THAT(victim.await_resume().has_value(), IsTrue());
    EXPECT_THAT(victim.await_resume().has_value(), IsTrue());
    EXPECT_THAT(victim.done(), IsTrue());
}

} // namespace sc::io::impl::uring::test
