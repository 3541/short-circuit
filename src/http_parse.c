#include "http_parse.h"

#include <assert.h>
#include <ctype.h>
#include <string.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "global.h"
#include "http_connection.h"
#include "http_response.h"
#include "http_types.h"
#include "ptr.h"
#include "ptr_util.h"
#include "uri.h"
#include "util.h"

#define _METHOD(M, N) { M, CS(N) },
static const struct {
    HttpMethod method;
    CString    name;
} HTTP_METHOD_NAMES[] = { HTTP_METHOD_ENUM };
#undef _METHOD

static HttpMethod http_request_method_parse(CByteString str) {
    assert(str.ptr && *str.ptr);

    CString method_str = cbstring_as_cstring(str);
    TRYB_MAP(method_str.ptr, HTTP_METHOD_INVALID);

    for (size_t i = 0;
         i < sizeof(HTTP_METHOD_NAMES) / sizeof(HTTP_METHOD_NAMES[0]); i++) {
        if (string_cmpi(method_str, HTTP_METHOD_NAMES[i].name) == 0)
            return HTTP_METHOD_NAMES[i].method;
    }

    return HTTP_METHOD_UNKNOWN;
}

#define _VERSION(V, S) { V, CS(S) },
static const struct {
    HttpVersion version;
    CString     str;
} HTTP_VERSION_STRINGS[] = { HTTP_VERSION_ENUM };
#undef _VERSION

CString http_version_string(HttpVersion version) {
    for (size_t i = 0;
         i < sizeof(HTTP_VERSION_STRINGS) / sizeof(HTTP_VERSION_STRINGS[0]);
         i++) {
        if (HTTP_VERSION_STRINGS[i].version == version)
            return HTTP_VERSION_STRINGS[i].str;
    }

    return CS_NULL;
}

static HttpVersion http_version_parse(CByteString str) {
    assert(str.ptr && *str.ptr);

    CString version_str = cbstring_as_cstring(str);
    TRYB_MAP(version_str.ptr, HTTP_VERSION_INVALID);

    for (size_t i = 0;
         i < sizeof(HTTP_VERSION_STRINGS) / sizeof(HTTP_VERSION_STRINGS[0]);
         i++) {
        if (string_cmpi(version_str, HTTP_VERSION_STRINGS[i].str) == 0)
            return HTTP_VERSION_STRINGS[i].version;
    }

    return HTTP_VERSION_UNKNOWN;
}

CString http_status_reason(HttpStatus status) {
#define _STATUS(CODE, TYPE, REASON) { CODE, CS(REASON) },
    static const struct {
        HttpStatus status;
        CString    reason;
    } HTTP_STATUS_REASONS[] = { HTTP_STATUS_ENUM };
#undef STATUS

    for (size_t i = 0;
         i < sizeof(HTTP_STATUS_REASONS) / sizeof(HTTP_STATUS_REASONS[0]);
         i++) {
        if (status == HTTP_STATUS_REASONS[i].status)
            return HTTP_STATUS_REASONS[i].reason;
    }

    return CS_NULL;
}

#define _CTYPE(T, S) { T, CS(S) },
static const struct {
    HttpContentType type;
    CString         str;
} HTTP_CONTENT_TYPE_NAMES[] = { HTTP_CONTENT_TYPE_ENUM };
#undef _CTYPE

CString http_content_type_name(HttpContentType type) {
    for (size_t i = 0; i < sizeof(HTTP_CONTENT_TYPE_NAMES) /
                               sizeof(HTTP_CONTENT_TYPE_NAMES[0]);
         i++) {
        if (type == HTTP_CONTENT_TYPE_NAMES[i].type)
            return HTTP_CONTENT_TYPE_NAMES[i].str;
    }

    return CS_NULL;
}

HttpContentType http_content_type_from_path(CString path) {
    assert(path.ptr);

    static struct {
        CString         ext;
        HttpContentType ctype;
    } EXTENSIONS[] = {
        { CS("bmp"), HTTP_CONTENT_TYPE_IMAGE_BMP },
        { CS("gif"), HTTP_CONTENT_TYPE_IMAGE_GIF },
        { CS("ico"), HTTP_CONTENT_TYPE_IMAGE_ICO },
        { CS("jpg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { CS("jpeg"), HTTP_CONTENT_TYPE_IMAGE_JPEG },
        { CS("png"), HTTP_CONTENT_TYPE_IMAGE_PNG },
        { CS("svg"), HTTP_CONTENT_TYPE_IMAGE_SVG },
        { CS("webp"), HTTP_CONTENT_TYPE_IMAGE_WEBP },
        { CS("css"), HTTP_CONTENT_TYPE_TEXT_CSS },
        { CS("js"), HTTP_CONTENT_TYPE_TEXT_JAVASCRIPT },
        { CS("txt"), HTTP_CONTENT_TYPE_TEXT_PLAIN },
        { CS("html"), HTTP_CONTENT_TYPE_TEXT_HTML },
    };

    CString last_dot = cstring_rchr(path, '.');
    if (!last_dot.ptr || last_dot.len < 2)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    CString last_slash = cstring_rchr(path, '/');
    if (last_slash.ptr && last_slash.ptr > last_dot.ptr)
        return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;

    CString ext = { .ptr = last_dot.ptr + 1, .len = last_dot.len - 1 };
    for (size_t i = 0; i < sizeof(EXTENSIONS) / sizeof(EXTENSIONS[0]); i++) {
        if (string_cmpi(ext, EXTENSIONS[i].ext) == 0)
            return EXTENSIONS[i].ctype;
    }

    return HTTP_CONTENT_TYPE_APPLICATION_OCTET_STREAM;
}

#define _TENCODING(E, S) { HTTP_##E, CS(S) },
static const struct {
    HttpTransferEncoding encoding;
    CString              value;
} HTTP_TRANSFER_ENCODING_VALUES[] = { HTTP_TRANSFER_ENCODING_ENUM };
#undef _TENCODING

static HttpTransferEncoding http_transfer_encoding_parse(CString value) {
    assert(value.ptr && *value.ptr);

    for (size_t i = 0; i < sizeof(HTTP_TRANSFER_ENCODING_VALUES) /
                               sizeof(HTTP_TRANSFER_ENCODING_VALUES[0]);
         i++) {
        if (string_cmpi(value, HTTP_TRANSFER_ENCODING_VALUES[i].value) == 0)
            return HTTP_TRANSFER_ENCODING_VALUES[i].encoding;
    }

    return HTTP_TRANSFER_ENCODING_INVALID;
}

// Try to parse the first line of the HTTP request.
HttpRequestStateResult http_request_first_line_parse(HttpConnection* this,
                                                     struct io_uring* uring) {
    assert(this);
    assert(uring);

    Buffer* buf = &this->conn.recv_buf;

    // If no CRLF has appeared so far, and the length of the data is
    // permissible, bail and wait for more.
    if (!buf_memmem(buf, HTTP_NEWLINE).ptr) {
        if (buf_len(buf) < HTTP_REQUEST_LINE_MAX_LENGTH)
            return 0;
        RET_MAP(http_response_error_submit(
                    this, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    this->method =
        http_request_method_parse(BS_CONST(buf_token_next(buf, CS(" "))));
    switch (this->method) {
    case HTTP_METHOD_INVALID:
        log_msg(TRACE, "Got an invalid method.");
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                2, -1);
    case HTTP_METHOD_UNKNOWN:
        log_msg(TRACE, "Got an unknown method.");
        RET_MAP(http_response_error_submit(this, uring,
                                           HTTP_STATUS_NOT_IMPLEMENTED,
                                           HTTP_RESPONSE_ALLOW),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    default:
        // Valid methods.
        break;
    }

    ByteString target_str = buf_token_next(buf, CS(" "));
    if (!target_str.ptr)
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    switch (uri_parse(&this->target, target_str)) {
    case URI_PARSE_ERROR:
    case URI_PARSE_BAD_URI:
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_ALLOW),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case URI_PARSE_TOO_LONG:
        RET_MAP(http_response_error_submit(
                    this, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    default:
        break;
    }

    this->target_path = uri_path_if_contained(&this->target, WEB_ROOT);
    if (!this->target_path.ptr)
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_NOT_FOUND,
                                           HTTP_RESPONSE_ALLOW),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    this->version = http_version_parse(
        BS_CONST(buf_token_next(buf, HTTP_NEWLINE, .preserve_end = true)));
    // Need to only eat one "\r\n".
    TRYB_MAP(buf_consume(buf, HTTP_NEWLINE), HTTP_REQUEST_STATE_ERROR);
    if (this->version == HTTP_VERSION_INVALID ||
        this->version == HTTP_VERSION_UNKNOWN ||
        (this->version == HTCPCP_VERSION_10 &&
         this->method != HTTP_METHOD_BREW)) {
        log_msg(TRACE, "Got a bad HTTP version.");
        this->version = HTTP_VERSION_11;
        RET_MAP(
            http_response_error_submit(this, uring,
                                       (this->version == HTTP_VERSION_INVALID)
                                           ? HTTP_STATUS_BAD_REQUEST
                                           : HTTP_STATUS_VERSION_NOT_SUPPORTED,
                                       HTTP_RESPONSE_CLOSE),
            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    } else if (this->version == HTTP_VERSION_10) {
        // HTTP/1.0 is 'Connection: Close' by default.
        this->keep_alive = false;
    }

    this->state = CONNECTION_PARSED_FIRST_LINE;

    return HTTP_REQUEST_STATE_DONE;
}

// Try to parse the first line of the HTTP request.
HttpRequestStateResult http_request_headers_parse(HttpConnection* this,
                                                  struct io_uring* uring) {
    assert(this);
    assert(uring);

    Buffer* buf = &this->conn.recv_buf;

    if (!buf_memmem(buf, HTTP_NEWLINE).ptr) {
        if (buf_len(buf) < HTTP_REQUEST_HEADER_MAX_LENGTH)
            return HTTP_REQUEST_STATE_NEED_DATA;
        RET_MAP(http_response_error_submit(this, uring,
                                           HTTP_STATUS_HEADER_TOO_LARGE,
                                           HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    if (!buf_consume(buf, HTTP_NEWLINE)) {
        while (buf->data.ptr[buf->head] != '\r' && buf->head != buf->tail) {
            if (!buf_memmem(buf, HTTP_NEWLINE).ptr)
                return HTTP_REQUEST_STATE_NEED_DATA;

            CString name  = S_CONST(buf_token_next_str(buf, CS(": ")));
            CString value = S_CONST(
                buf_token_next_str(buf, HTTP_NEWLINE, .preserve_end = true));
            TRYB_MAP(buf_consume(buf, HTTP_NEWLINE), HTTP_REQUEST_STATE_ERROR);

            // RFC7230 § 5.4: Invalid field-value -> 400.
            if (!name.ptr || !value.ptr)
                RET_MAP(http_response_error_submit(this, uring,
                                                   HTTP_STATUS_BAD_REQUEST,
                                                   HTTP_RESPONSE_CLOSE),
                        HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

            // TODO: Handle general headers.
            if (string_cmpi(name, CS("Connection")) == 0)
                this->keep_alive = string_cmpi(value, CS("Keep-Alive")) == 0;
            else if (string_cmpi(name, CS("Host")) == 0) {
                // ibid. >1 Host header -> 400.
                if (this->host.ptr)
                    RET_MAP(http_response_error_submit(this, uring,
                                                       HTTP_STATUS_BAD_REQUEST,
                                                       HTTP_RESPONSE_CLOSE),
                            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

                this->host = S_CONST(string_clone(value));
            } else if (string_cmpi(name, CS("Transfer-Encoding")) == 0) {
                this->transfer_encodings |= http_transfer_encoding_parse(value);
                if (this->transfer_encodings & HTTP_TRANSFER_ENCODING_INVALID)
                    RET_MAP(http_response_error_submit(this, uring,
                                                       HTTP_STATUS_BAD_REQUEST,
                                                       HTTP_RESPONSE_CLOSE),
                            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
            } else if (string_cmpi(name, CS("Content-Length")) == 0) {
                char*   endptr     = NULL;
                ssize_t new_length = strtol(value.ptr, &endptr, 10);

                // RFC 7230 § 3.3.3, step 4: Invalid or conflicting
                // Content-Length
                // -> 400.
                if (*endptr != '\0' || (this->content_length != -1 &&
                                        this->content_length != new_length))
                    RET_MAP(http_response_error_submit(this, uring,
                                                       HTTP_STATUS_BAD_REQUEST,
                                                       HTTP_RESPONSE_CLOSE),
                            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

                if ((size_t)new_length > HTTP_REQUEST_CONTENT_MAX_LENGTH)
                    RET_MAP(http_response_error_submit(
                                this, uring, HTTP_STATUS_PAYLOAD_TOO_LARGE,
                                HTTP_RESPONSE_CLOSE),
                            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

                this->content_length = new_length;
            }
        }

        if (!buf_consume(buf, HTTP_NEWLINE))
            return HTTP_REQUEST_STATE_NEED_DATA;
    }

    // RFC7230 § 3.3.3, step 3: Transfer-Encoding without chunked is invalid in
    // a request, and the server MUST respond with a 400.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0 &&
        (this->transfer_encodings & HTTP_TRANSFER_ENCODING_CHUNKED) == 0)
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    // ibid. Transfer-Encoding overrides Content-Length.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0 &&
        this->content_length >= 0)
        this->content_length = -1;

    // TODO: Support other transfer encodings.
    if ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) != 0)
        RET_MAP(http_response_error_submit(this, uring,
                                           HTTP_STATUS_NOT_IMPLEMENTED,
                                           HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    // RFC7230 § 3.3.3, step 6: default to Content-Length of 0.
    if (this->content_length == -1 &&
        ((this->transfer_encodings & ~HTTP_TRANSFER_ENCODING_IDENTITY) == 0)) {
        this->content_length = 0;
    }

    // RFC7230 § 5.4: HTTP/1.1 messages must have a Host header.
    if (this->version == HTTP_VERSION_11 && !this->host.ptr)
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_BAD_REQUEST,
                                           HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    this->state = CONNECTION_PARSED_HEADERS;

    return HTTP_REQUEST_STATE_DONE;
}
