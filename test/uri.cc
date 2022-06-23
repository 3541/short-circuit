#include <string_view>

#include <gtest/gtest.h>

#include <a3/str.h>

#include <sc/uri.h>

using namespace testing;

class UriTest : public Test {
protected:
    ScUri uri; // NOLINT(misc-non-private-member-variables-in-classes)

    void TearDown() override {
        if (sc_uri_is_initialized(&uri))
            a3_string_free(&uri.data);
    }
};

TEST_F(UriTest, parse_trivial) {
    A3String s = a3_string_clone(A3_CS("/test.txt"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);

    EXPECT_EQ(uri.scheme, SC_URI_SCHEME_UNSPECIFIED);
    EXPECT_FALSE(uri.authority.ptr);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);

    a3_string_free(&s);
}

TEST_F(UriTest, parse_scheme_authority) {
    A3String s1 = a3_string_clone(A3_CS("http://example.com/test.txt"));
    A3String s2 = a3_string_clone(A3_CS("https://example.com/asdf.txt"));

    EXPECT_EQ(sc_uri_parse(&uri, s1), SC_URI_PARSE_OK);
    EXPECT_EQ(uri.scheme, SC_URI_SCHEME_HTTP);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);

    EXPECT_EQ(sc_uri_parse(&uri, s2), SC_URI_PARSE_OK);
    EXPECT_EQ(uri.scheme, SC_URI_SCHEME_HTTPS);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/asdf.txt")), 0);
    EXPECT_FALSE(uri.query.ptr);

    a3_string_free(&s1);
    a3_string_free(&s2);
}

TEST_F(UriTest, reject_invalid_scheme) {
    A3String s = a3_string_clone(A3_CS("gopher://example.com/test.txt"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);

    a3_string_free(&s);
}

TEST_F(UriTest, parse_components) {
    A3String s = a3_string_clone(A3_CS("http://example.com/test.txt?query=1#fragment"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);
    EXPECT_EQ(uri.scheme, SC_URI_SCHEME_HTTP);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.authority), A3_CS("example.com")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/test.txt")), 0);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.query), A3_CS("query=1")), 0);

    a3_string_free(&s);
}

TEST_F(UriTest, reject_escape) {
    A3String s = a3_string_clone(A3_CS("http://example.com/../.."));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);

    a3_string_free(&s);
}

TEST_F(UriTest, parse_not_escape) {
    A3String s = a3_string_clone(A3_CS("http://example.com/..."));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);
    EXPECT_EQ(a3_string_cmp(A3_S_CONST(uri.path), A3_CS("/...")), 0);

    a3_string_free(&s);
}

TEST_F(UriTest, parse_normalize) {
    A3String s =
        a3_string_clone(A3_CS("http://example.com/a/b/.long/d/.././also_long/./f/../../g"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);
    EXPECT_EQ(std::string_view { uri.path }, "/a/b/.long/g");

    a3_string_free(&s);
}

TEST_F(UriTest, parse_percent_encoding) {
    A3String s = a3_string_clone(A3_CS("http://example.com/abc%20xyz%5buvw%5B"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);
    EXPECT_EQ(std::string_view { uri.path }, "/abc xyz[uvw[");

    a3_string_free(&s);
}

TEST_F(UriTest, reject_malformed_percent) {
    A3String s = a3_string_clone(A3_CS("http://example.com/abc%ZZ"));
    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);
    a3_string_free(&s);

    s = a3_string_clone(A3_CS("http://example.com/abc%a-"));
    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);
    a3_string_free(&s);

    s = a3_string_clone(A3_CS("http://example.com/abc%a"));
    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);
    a3_string_free(&s);
}

TEST_F(UriTest, reject_null_percent) {
    A3String s = a3_string_clone(A3_CS("http://example.com/abc%00"));
    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_BAD_URI);
    a3_string_free(&s);
}

TEST_F(UriTest, uri_path_relative) {
    A3String s = a3_string_clone(A3_CS("http://example.com/abc"));

    EXPECT_EQ(sc_uri_parse(&uri, s), SC_URI_PARSE_OK);
    EXPECT_EQ(std::string_view { sc_uri_path_relative(&uri) }, "abc");

    a3_string_free(&s);
}
