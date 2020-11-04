#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "forward.h"
#include "ptr.h"

#define URI_SCHEME_ENUM                                                        \
    _SCHEME(URI_SCHEME_UNSPECIFIED, "")                                        \
    _SCHEME(URI_SCHEME_HTTP, "http")                                           \
    _SCHEME(URI_SCHEME_HTTPS, "https")                                         \
    _SCHEME(URI_SCHEME_INVALID, "")

enum UriScheme {
#define _SCHEME(T, S) T,
    URI_SCHEME_ENUM
#undef _SCHEME
};

struct Uri {
    enum UriScheme scheme;
    String         authority;
    ByteString     path;
    ByteString     query;
    ByteString     fragment;
};

#define URI_PARSE_ERROR    -2
#define URI_PARSE_BAD_URI  -1
#define URI_PARSE_TOO_LONG 0
#define URI_PARSE_SUCCESS  1

int8_t uri_parse(struct Uri*, ByteString);
bool   uri_path_is_contained(struct Uri*, CString real_root);
bool   uri_is_initialized(struct Uri*);
void   uri_free(struct Uri*);
