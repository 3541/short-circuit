#pragma once

#include <stdbool.h>

#include <a3/str.h>

#define URI_SCHEME_ENUM                                                        \
    _SCHEME(URI_SCHEME_UNSPECIFIED, "")                                        \
    _SCHEME(URI_SCHEME_HTTP, "http")                                           \
    _SCHEME(URI_SCHEME_HTTPS, "https")                                         \
    _SCHEME(URI_SCHEME_INVALID, "")

typedef enum UriScheme {
#define _SCHEME(T, S) T,
    URI_SCHEME_ENUM
#undef _SCHEME
} UriScheme;

typedef struct Uri {
    UriScheme scheme;
    String    authority;
    String    path;
    String    query;
    String    fragment;
} Uri;

typedef enum UriParseResult {
    URI_PARSE_ERROR,
    URI_PARSE_BAD_URI,
    URI_PARSE_TOO_LONG,
    URI_PARSE_SUCCESS
} UriParseResult;

UriParseResult uri_parse(Uri*, String);
String         uri_path_if_contained(Uri*, CString real_root);
bool           uri_is_initialized(Uri*);
void           uri_free(Uri*);
