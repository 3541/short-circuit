#include "http/parse.h"

#include <assert.h>
#include <liburing.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "global.h"
#include "http/connection.h"
#include "http/response.h"
#include "http/types.h"
#include "uri.h"

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
            return HTTP_REQUEST_STATE_NEED_DATA;
        RET_MAP(http_response_error_submit(
                    this, uring, HTTP_STATUS_URI_TOO_LONG, HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    }

    this->method =
        http_request_method_parse(S_CONST(buf_token_next(buf, CS(" "))));
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

    String target_str = buf_token_next(buf, CS(" "));
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
        S_CONST(buf_token_next(buf, HTTP_NEWLINE, .preserve_end = true)));
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

            CString name  = S_CONST(buf_token_next(buf, CS(": ")));
            CString value = S_CONST(
                buf_token_next(buf, HTTP_NEWLINE, .preserve_end = true));
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

                this->host = string_clone(value);
            } else if (string_cmpi(name, CS("Transfer-Encoding")) == 0) {
                this->transfer_encodings |= http_transfer_encoding_parse(value);
                if (this->transfer_encodings & HTTP_TRANSFER_ENCODING_INVALID)
                    RET_MAP(http_response_error_submit(this, uring,
                                                       HTTP_STATUS_BAD_REQUEST,
                                                       HTTP_RESPONSE_CLOSE),
                            HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
            } else if (string_cmpi(name, CS("Content-Length")) == 0) {
                char* endptr = NULL;
                // NOTE: This depends on the fact that the buf_token_next
                // null-terminates strings.
                ssize_t new_length =
                    strtol((const char*)value.ptr, &endptr, 10);

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
