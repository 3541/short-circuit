#include <cstdlib>
#include <string>
#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <sc/io/file.hh>

namespace sc::io::test {

using namespace testing;

static_assert(!std::is_copy_constructible_v<File>);
static_assert(!std::is_copy_assignable_v<File>);

static_assert(std::is_move_constructible_v<File>);
static_assert(std::is_move_assignable_v<File>);

static_assert(sizeof(File) <= sizeof(void*));

// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
struct FileTest : public Test {
private:
    std::string m_temp_name{"XXXXXX"};
    int         m_temp;

public:
    FileTest() : m_temp{mkstemp(m_temp_name.data())} {}
    ~FileTest() {
        unlink(m_temp_name.c_str());
        close(m_temp);
    }

    int temp_file() const { return m_temp; }
};

TEST(File, create) {
    auto file = File::unowned(0);

    EXPECT_THAT(file.fd(), Eq(0));
}

TEST_F(FileTest, closesOwnedFile) {
    {
        auto file = File::owned(temp_file());

        EXPECT_THAT(write(file.fd(), "A", 1), Eq(1));
    }

    EXPECT_THAT(write(temp_file(), "A", 1), Lt(0));
}

TEST_F(FileTest, leavesOpenUnownedFile) {
    {
        auto file = File::unowned(temp_file());

        EXPECT_THAT(write(file.fd(), "A", 1), Eq(1));
    }

    EXPECT_THAT(write(temp_file(), "A", 1), Eq(1));
}

} // namespace sc::io::test
