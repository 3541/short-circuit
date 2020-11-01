#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "forward.h"

#define HTTP_METHOD_ENUM                                                       \
    _METHOD(HTTP_METHOD_INVALID, "__INVALID")                                  \
    _METHOD(HTTP_METHOD_UNKNOWN, "__UNKNOWN")

enum HttpMethod {
#define _METHOD(M, N) M,
    HTTP_METHOD_ENUM
#undef _METHOD
};

#define HTTP_VERSION_ENUM                                                      \
    _VERSION(HTTP_VERSION_INVALID, "")                                         \
    _VERSION(HTTP_VERSION_10, "HTTP/1.0")                                      \
    _VERSION(HTTP_VERSION_11, "HTTP/1.1")

enum HttpVersion {
#define _VERSION(V, S) V,
    HTTP_VERSION_ENUM
#undef _VERSION
};

#define HTTP_CONTENT_TYPE_ENUM                                                 \
    _CTYPE(HTTP_CONTENT_TYPE_INVALID, "")                                      \
    _CTYPE(HTTP_CONTENT_TYPE_TEXT_HTML, "text/html")

enum HttpContentType {
#define _CTYPE(T, S) T,
    HTTP_CONTENT_TYPE_ENUM
#undef _CTYPE
};

#define HTTP_STATUS_ENUM                                                       \
    _STATUS(0, HTTP_STATUS_INVALID, "Invalid error")                           \
    _STATUS(400, HTTP_STATUS_BAD_REQUEST, "Bad Request")                       \
    _STATUS(414, HTTP_STATUS_URI_TOO_LONG, "URI Too Long")                     \
    _STATUS(501, HTTP_STATUS_NOT_IMPLEMENTED, "Not Implemented")

enum HttpStatus {
#define _STATUS(CODE, TYPE, REASON) TYPE = CODE,
    HTTP_STATUS_ENUM
#undef _STATUS
};

enum HttpRequestState {
    REQUEST_INIT,
    REQUEST_PARSED_FIRST_LINE,
    REQUEST_PARSED_HEADERS,
    REQUEST_RESPONDING,
    REQUEST_CLOSING,
};

struct HttpRequest {
    enum HttpRequestState state;

    enum HttpVersion version;
    enum HttpMethod  method;
    bool             keep_alive;

    enum HttpContentType content_type;
};

int8_t http_request_handle(struct Connection*, struct io_uring*);
bool   http_response_handle(struct Connection*, struct io_uring*);
