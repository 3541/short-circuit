#include <memory>

#include <gmock/gmock.h>

import sc.lib.nonnull;

namespace sc::lib::test {

namespace {

using namespace testing;

struct S {
    int m_value;

    constexpr bool operator==(S const&) const noexcept = default;
};

} // namespace

TEST(nonnull, null) { EXPECT_DEATH(NonNull{static_cast<int*>(nullptr)}, "from a null pointer"); }

TEST(nonnull, raw) {
    NonNull victim{new S{43}};
    static_assert(std::same_as<decltype(victim)::element_type, S>);

    EXPECT_THAT(*victim, Eq(S{43}));
    EXPECT_THAT(victim->m_value, Eq(43));

    delete &*victim;
}

TEST(nonnull, unique) {
    NonNull victim{std::make_unique<S>(800)};
    static_assert(std::same_as<decltype(victim)::element_type, S>);

    EXPECT_THAT(*victim, Eq(S{800}));
    EXPECT_THAT(victim->m_value, Eq(800));

    NonNull other{std::move(victim)};
    static_assert(std::same_as<decltype(other)::element_type, S>);

    EXPECT_THAT(*other, Eq(S{800}));
}

TEST(nonnull, shared) {
    NonNull victim{std::make_shared<S>(22)};
    static_assert(std::same_as<decltype(victim)::element_type, S>);

    EXPECT_THAT(*victim, Eq(S{22}));
    EXPECT_THAT(victim->m_value, Eq(22));

    NonNull other{victim};
    static_assert(std::same_as<decltype(other)::element_type, S>);

    EXPECT_THAT(victim->m_value, Eq(22));
    EXPECT_THAT(other->m_value, Eq(22));
    EXPECT_THAT(&*victim, Eq(&*other));
}

TEST(nonnull, array) {
    NonNull<int[]> victim{new int[3]{1, 2, 3}};
    static_assert(std::same_as<decltype(victim)::Inner, int[]>);
    static_assert(std::same_as<decltype(victim)::element_type, int>);
    static_assert(std::same_as<decltype(victim)::value_type, int>);

    EXPECT_THAT(victim[0], Eq(1));
    EXPECT_THAT(victim[1], Eq(2));
    EXPECT_THAT(victim[2], Eq(3));

    delete[] &*victim;
}

TEST(nonnull, unique_array) {
    NonNull victim{std::make_unique<int[]>(3)};
    static_assert(std::same_as<decltype(victim)::Inner, std::unique_ptr<int[]>>);
    static_assert(std::same_as<decltype(victim)::element_type, int>);
    static_assert(std::same_as<decltype(victim)::value_type, int>);

    victim[0] = 4;
    EXPECT_THAT(victim[0], Eq(4));
}

} // namespace sc::lib::test
