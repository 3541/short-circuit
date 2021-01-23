#include <gtest/gtest.h>

#include <a3/str.h>

#include "uri.h"

class UriTest : public ::testing::Test {
protected:
    Uri uri {};

    void TearDown() override { uri_free(&uri); }
};

TEST_F(UriTest, parse_trivial) {
    A3String s = a3_string_clone(A3_CS("/test.txt"));

    EXPECT_EQ(uri_parse(&uri, s), URI_PARSE_SUCCESS);

    EXPECT_EQ(uri.scheme, URI_SCHEME_UNSPECIFIED);
    EXPECT_FALSE(uri.authority.ptr);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);
    EXPECT_FALSE(uri.fragment.ptr);

    a3_string_free(&s);
}
