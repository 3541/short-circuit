#include <cstddef>
#include <vector>

#include <gmock/gmock.h>

import sc.io.alloc;
import sc.io.buf;

namespace sc::io::test {

using namespace testing;

TEST(Alloc, basic) {
    auto& victim = Allocator::the();

    std::size_t expected_count;
    {
        std::vector<Buf> buffers;
        victim.consume_free([&](Buf buf) { buffers.push_back(std::move(buf)); });
        expected_count = buffers.size();

        EXPECT_THAT(buffers.empty(), IsFalse());

        std::swap(buffers.back(), buffers[buffers.size() / 4]);
        auto const id = buffers.back().id();
        buffers.pop_back();

        std::optional<Buf> buf;
        victim.consume_free([&](Buf b) {
            EXPECT_THAT(buf, IsFalse());
            buf = std::move(b);
        });
        ASSERT_THAT(buf, IsTrue());
        EXPECT_THAT(buf->id(), Eq(id));

        std::size_t count = 0;
        victim.consume_free([&](Buf) { ++count; });
        EXPECT_THAT(count, Eq(0));
    }

    std::vector<Buf> buffers;
    victim.consume_free([&](Buf buf) { buffers.push_back(std::move(buf)); });
    EXPECT_THAT(buffers.size(), Eq(expected_count));
}

} // namespace sc::io::test
