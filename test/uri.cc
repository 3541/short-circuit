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

TEST_F(UriTest, parse_scheme_authority) {
    A3String s1 = a3_string_clone(A3_CS("http://example.com/test.txt"));
    A3String s2 = a3_string_clone(A3_CS("https://example.com/asdf.txt"));

    EXPECT_EQ(uri_parse(&uri, s1), URI_PARSE_SUCCESS);
    EXPECT_EQ(uri.scheme, URI_SCHEME_HTTP);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);
    EXPECT_FALSE(uri.fragment.ptr);
    uri_free(&uri);

    EXPECT_EQ(uri_parse(&uri, s2), URI_PARSE_SUCCESS);
    EXPECT_EQ(uri.scheme, URI_SCHEME_HTTPS);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/asdf.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);
    EXPECT_FALSE(uri.fragment.ptr);

    a3_string_free(&s1);
    a3_string_free(&s2);
}

TEST_F(UriTest, parse_components) {
    A3String s = a3_string_clone(A3_CS("http://example.com/test.txt?query=1#fragment"));

    EXPECT_EQ(uri_parse(&uri, s), URI_PARSE_SUCCESS);
    EXPECT_EQ(uri.scheme, URI_SCHEME_HTTP);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.query), A3_CS("query=1")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.fragment), A3_CS("fragment")), 0);

    a3_string_free(&s);
}
