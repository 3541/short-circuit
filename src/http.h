#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "forward.h"

#define HTTP_METHOD_ENUM                                                       \
    _METHOD(HTTP_METHOD_INVALID, "__INVALID")                                  \
    _METHOD(HTTP_METHOD_GET, "GET")                                            \
    _METHOD(HTTP_METHOD_BREW, "BREW")                                          \
    _METHOD(HTTP_METHOD_UNKNOWN, "__UNKNOWN")

enum HttpMethod {
#define _METHOD(M, N) M,
    HTTP_METHOD_ENUM
#undef _METHOD
};

#define HTTP_VERSION_ENUM                                                      \
    _VERSION(HTTP_VERSION_INVALID, "")                                         \
    _VERSION(HTTP_VERSION_10, "HTTP/1.0")                                      \
    _VERSION(HTTP_VERSION_11, "HTTP/1.1")                                      \
    _VERSION(HTTP_VERSION_UNKNOWN, "")

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
    _STATUS(413, HTTP_STATUS_PAYLOAD_TOO_LARGE, "Payload Too Large")           \
    _STATUS(414, HTTP_STATUS_URI_TOO_LONG, "URI Too Long")                     \
    _STATUS(418, HTTP_STATUS_IM_A_TEAPOT, "I'm a teapot")                      \
    _STATUS(431, HTTP_STATUS_HEADER_TOO_LARGE,                                 \
            "Request Header Fields Too Large")                                 \
    _STATUS(501, HTTP_STATUS_NOT_IMPLEMENTED, "Not Implemented")               \
    _STATUS(505, HTTP_STATUS_VERSION_NOT_SUPPORTED,                            \
            "HTTP Version Not Supported")

enum HttpStatus {
#define _STATUS(CODE, TYPE, REASON) TYPE = CODE,
    HTTP_STATUS_ENUM
#undef _STATUS
};

#define HTTP_TRANSFER_ENCODING_ENUM                                            \
    _TENCODING(TRANSFER_ENCODING_IDENTITY, "identity")                         \
    _TENCODING(TRANSFER_ENCODING_CHUNKED, "chunked")

enum HttpTransferBits {
#define _TENCODING(E, S) _HTTP_##E,
    HTTP_TRANSFER_ENCODING_ENUM
#undef _TENCODING
};

typedef uint8_t HttpTransferEncoding;
#define _TENCODING(E, S)                                                       \
    static const HttpTransferEncoding HTTP_##E = 1 << (_HTTP_##E);
HTTP_TRANSFER_ENCODING_ENUM
#undef _TENCODING
static const HttpTransferEncoding HTTP_TRANSFER_ENCODING_INVALID = 0;

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
    char*            target;

    bool                 keep_alive;
    const char*          host;
    HttpTransferEncoding transfer_encodings;
    ssize_t              content_length;

    enum HttpContentType response_content_type;
    HttpTransferEncoding response_transfer_encodings;
};

void   http_request_reset(struct HttpRequest*);
int8_t http_request_handle(struct Connection*, struct io_uring*);
bool   http_response_handle(struct Connection*, struct io_uring*);
