#include <gmock/gmock.h>

import sc.lib.deque;

namespace sc::lib::test {

using namespace testing;

TEST(deque, construction) {
    Deque<int> victim;
    EXPECT_THAT(victim.len(), Eq(0));
    EXPECT_THAT(victim.is_empty(), IsTrue());
}

TEST(deque, access) {
    Deque<int> victim{1, 2, 3, 4, 5};
    EXPECT_THAT(victim.is_empty(), IsFalse());
    EXPECT_THAT(victim.len(), Eq(5));
    EXPECT_THAT(victim, ElementsAre(1, 2, 3, 4, 5));
    EXPECT_THAT(victim[2], Eq(3));
}

TEST(deque, queue) {
    Deque<int> victim;

    victim.push_back(5);
    victim.push_back(2);
    EXPECT_THAT(victim, ElementsAre(5, 2));

    victim.push_front(1);
    EXPECT_THAT(victim, ElementsAre(1, 5, 2));

    EXPECT_THAT(victim.pop_back(), Eq(2));
    EXPECT_THAT(victim, ElementsAre(1, 5));

    EXPECT_THAT(victim.pop_front(), Eq(1));
    EXPECT_THAT(victim, ElementsAre(5));

    EXPECT_THAT(victim.pop_front(), Eq(5));
    EXPECT_THAT(victim.is_empty(), IsTrue());
}

TEST(deque, emplace) {
    struct S {
        int m_value;

        explicit S(int value) : m_value{value} {}

        constexpr bool operator==(S const&) const noexcept = default;
    };

    Deque<S> victim;

    victim.emplace_back(5);
    victim.emplace_back(2);
    EXPECT_THAT(victim, ElementsAre(S{5}, S{2}));

    victim.emplace_front(1);
    EXPECT_THAT(victim, ElementsAre(S{1}, S{5}, S{2}));
}

} // namespace sc::lib::test
