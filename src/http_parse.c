#include "http_parse.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "http.h"
#include "util.h"

#define _METHOD(M, N) { M, N },
static const struct {
    enum HttpMethod method;
    const char*     name;
} HTTP_METHOD_NAMES[] = { HTTP_METHOD_ENUM };
#undef _METHOD

static enum HttpMethod http_request_method_parse(const uint8_t* str) {
    assert(str && *str);

    for (const uint8_t* sp = str; *sp; sp++)
        if (!isalpha(*sp))
            return HTTP_METHOD_INVALID;

    const char* method_str = (const char*)str;
    for (size_t i = 0;
         i < sizeof(HTTP_METHOD_NAMES) / sizeof(HTTP_METHOD_NAMES[0]); i++) {
        if (strcasecmp(method_str, HTTP_METHOD_NAMES[i].name) == 0)
            return HTTP_METHOD_NAMES[i].method;
    }

    return HTTP_METHOD_UNKNOWN;
}

#define _VERSION(V, S) { V, S },
static const struct {
    enum HttpVersion version;
    const char*      str;
} HTTP_VERSION_STRINGS[] = { HTTP_VERSION_ENUM };
#undef _VERSION

const char* http_version_string(enum HttpVersion version) {
    for (size_t i = 0;
         i < sizeof(HTTP_VERSION_STRINGS) / sizeof(HTTP_VERSION_STRINGS[0]);
         i++) {
        if (HTTP_VERSION_STRINGS[i].version == version)
            return HTTP_VERSION_STRINGS[i].str;
    }

    return NULL;
}

static enum HttpVersion http_version_parse(const uint8_t* str) {
    assert(str && *str);

    for (const uint8_t* sp = str; *sp; sp++)
        if (!isprint(*sp))
            return HTTP_VERSION_INVALID;

    const char* version_str = (const char*)str;
    for (size_t i = 0;
         i < sizeof(HTTP_VERSION_STRINGS) / sizeof(HTTP_VERSION_STRINGS[0]);
         i++) {
        if (strcasecmp(version_str, HTTP_VERSION_STRINGS[i].str) == 0)
            return HTTP_VERSION_STRINGS[i].version;
    }

    return HTTP_VERSION_UNKNOWN;
}

const char* http_status_reason(enum HttpStatus status) {
#define _STATUS(CODE, TYPE, REASON) { CODE, REASON },
    static const struct {
        enum HttpStatus status;
        const char*     reason;
    } HTTP_STATUS_REASONS[] = { HTTP_STATUS_ENUM{ 0, NULL } };
#undef STATUS

    for (size_t i = 0;
         i < sizeof(HTTP_STATUS_REASONS) / sizeof(HTTP_STATUS_REASONS[0]);
         i++) {
        if (status == HTTP_STATUS_REASONS[i].status)
            return HTTP_STATUS_REASONS[i].reason;
    }

    return NULL;
}

#define _CTYPE(T, S) { T, S },
static const struct {
    enum HttpContentType type;
    const char*          str;
} HTTP_CONTENT_TYPE_NAMES[] = { HTTP_CONTENT_TYPE_ENUM };
#undef _CTYPE

const char* http_content_type_name(enum HttpContentType type) {
    for (size_t i = 0; i < sizeof(HTTP_CONTENT_TYPE_NAMES) /
                               sizeof(HTTP_CONTENT_TYPE_NAMES[0]);
         i++) {
        if (type == HTTP_CONTENT_TYPE_NAMES[i].type)
            return HTTP_CONTENT_TYPE_NAMES[i].str;
    }

    return NULL;
}

#define _TENCODING(E, S) { HTTP_##E, S },
static const struct {
    HttpTransferEncoding encoding;
    const char*          value;
} HTTP_TRANSFER_ENCODING_VALUES[] = { HTTP_TRANSFER_ENCODING_ENUM };
#undef _TENCODING

static HttpTransferEncoding http_transfer_encoding_parse(const char* value) {
    for (size_t i = 0; i < sizeof(HTTP_TRANSFER_ENCODING_VALUES) /
                               sizeof(HTTP_TRANSFER_ENCODING_VALUES[0]);
         i++) {
        if (strcasecmp(value, HTTP_TRANSFER_ENCODING_VALUES[i].value) == 0)
            return HTTP_TRANSFER_ENCODING_VALUES[i].encoding;
    }

    return HTTP_TRANSFER_ENCODING_INVALID;
}

// Try to parse the first line of the HTTP request.
// Returns:
//   -1 on error.
//    0 for more data.
//    1 on completion.
//    2 to bail successfully (for HTTP errors, which are "successful" from the
//    perspective of a connection).
int8_t http_request_first_line_parse(struct Connection* conn,
                                     struct io_uring*   uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;
    struct Buffer* buf       = &conn->recv_buf;

    // If no CRLF has appeared so far, and the length of the data is
    // permissible, bail and wait for more.
    if (!buf_memmem(buf, HTTP_NEWLINE)) {
        if (buf_len(buf) < HTTP_REQUEST_LINE_MAX_LENGTH)
            return 0;
        RET_MAP(http_response_error_submit(
                    conn, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
                2, -1);
    }

    this->method = http_request_method_parse(buf_token_next(buf, " "));
    switch (this->method) {
    case HTTP_METHOD_INVALID:
        log_msg(TRACE, "Got an invalid method.");
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);
    case HTTP_METHOD_UNKNOWN:
        log_msg(TRACE, "Got an unknown method.");
        RET_MAP(http_response_error_submit(conn, uring,
                                           HTTP_STATUS_NOT_IMPLEMENTED,
                                           HTTP_RESPONSE_ALLOW),
                2, -1);
    case HTTP_METHOD_BREW:
        log_msg(TRACE, "I'm a teapot.");
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_IM_A_TEAPOT,
                                           HTTP_RESPONSE_ALLOW),
                2, -1);
    default:
        break;
    }

    this->target = strndup((char*)buf_token_next(buf, " "),
                           HTTP_REQUEST_URI_MAX_LENGTH + 1);
    if (strlen(this->target) > HTTP_REQUEST_URI_MAX_LENGTH) {
        log_msg(TRACE, "Request URI is too long.");
        RET_MAP(http_response_error_submit(
                    conn, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
                2, -1);
    }

    this->version = http_version_parse(buf_token_next(buf, HTTP_NEWLINE));
    if (this->version == HTTP_VERSION_INVALID ||
        this->version == HTTP_VERSION_UNKNOWN) {
        log_msg(TRACE, "Got a bad HTTP version.");
        this->version = HTTP_VERSION_11;
        RET_MAP(
            http_response_error_submit(conn, uring,
                                       (this->version == HTTP_VERSION_INVALID)
                                           ? HTTP_STATUS_BAD_REQUEST
                                           : HTTP_STATUS_VERSION_NOT_SUPPORTED,
                                       HTTP_RESPONSE_CLOSE),
            2, -1);
    } else if (this->version == HTTP_VERSION_10) {
        // HTTP/1.0 is 'Connection: Close' by default.
        this->keep_alive = false;
    }

    this->state = REQUEST_PARSED_FIRST_LINE;

    return 1;
}

// Try to parse the first line of the HTTP request.
// Returns:
//   -1 on error.
//    0 for more data.
//    1 on completion.
//    2 to bail successfully (for HTTP errors, which are "successful" from the
//    perspective of a connection).
int8_t http_request_headers_parse(struct Connection* conn,
                                  struct io_uring*   uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;
    struct Buffer* buf       = &conn->recv_buf;

    if (!buf_memmem(buf, HTTP_NEWLINE)) {
        if (buf_len(buf) < HTTP_REQUEST_HEADER_MAX_LENGTH)
            return 0;
        RET_MAP(http_response_error_submit(conn, uring,
                                           HTTP_STATUS_HEADER_TOO_LARGE,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);
    }

    while (buf->data[buf->head] != '\r' && buf->head != buf->tail) {
        if (!buf_memmem(buf, HTTP_NEWLINE))
            return 0;

        char* name  = (char*)buf_token_next(buf, ": ");
        char* value = (char*)buf_token_next(buf, HTTP_NEWLINE);

        // TODO: Handle general headers.
        if (strcasecmp(name, "Connection") == 0)
            this->keep_alive = strcasecmp(value, "Keep-Alive") == 0;
        else if (strcasecmp(name, "Host") == 0) {
            // RFC7230 § 5.4: >1 Host header -> 400.
            if (this->host)
                RET_MAP(http_response_error_submit(conn, uring,
                                                   HTTP_STATUS_BAD_REQUEST,
                                                   HTTP_RESPONSE_CLOSE),
                        2, -1)

            this->host = strndup(value, HTTP_REQUEST_HOST_MAX_LENGTH);

            // ibid. Invalid field-value -> 400.
            for (const char* sp = this->host; sp && *sp; sp++)
                if (!isgraph(*sp))
                    RET_MAP(http_response_error_submit(conn, uring,
                                                       HTTP_STATUS_BAD_REQUEST,
                                                       HTTP_RESPONSE_CLOSE),
                            2, -1);
        } else if (strcasecmp(name, "Transfer-Encoding") == 0)
            this->transfer_encodings |= http_transfer_encoding_parse(value);
        else if (strcasecmp(name, "Content-Length") == 0) {
            char*   endptr     = NULL;
            ssize_t new_length = strtol(value, &endptr, 10);

            // RFC 7230 § 3.3.3, step 4: Invalid or conflicting Content-Length
            // -> 400.
            if (*endptr != '\0' || (this->content_length != -1 &&
                                    this->content_length != new_length))
                RET_MAP(http_response_error_submit(conn, uring,
                                                   HTTP_STATUS_BAD_REQUEST,
                                                   HTTP_RESPONSE_CLOSE),
                        2, -1);

            if ((size_t)new_length > HTTP_REQUEST_CONTENT_MAX_LENGTH)
                RET_MAP(http_response_error_submit(
                            conn, uring, HTTP_STATUS_PAYLOAD_TOO_LARGE,
                            HTTP_RESPONSE_CLOSE),
                        2, -1);

            this->content_length = new_length;
        }
    }

    if (!buf_consume(buf, HTTP_NEWLINE))
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);

    // RFC7230 § 3.3.3, step 3: Transfer-Encoding without chunked is invalid in
    // a request, and the server MUST respond with a 400.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0 &&
        (this->transfer_encodings & HTTP_TRANSFER_ENCODING_CHUNKED) == 0)
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);

    // ibid.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0 &&
        this->content_length >= 0)
        this->content_length = -1;

    // TODO: Support other transfer encodings.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0)
        RET_MAP(http_response_error_submit(conn, uring,
                                           HTTP_STATUS_NOT_IMPLEMENTED,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);

    // RFC7230 § 3.3.3, step 6: default to Content-Length of 0.
    if (this->content_length == -1 &&
        ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) == 0)) {
        this->content_length = 0;
    }

    // RFC7230 § 5.4: HTTP/1.1 messages must have a Host header.
    if (this->version == HTTP_VERSION_11 && !this->host)
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);

    this->state = REQUEST_PARSED_HEADERS;

    return 1;
}
