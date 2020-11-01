#include "http.h"

#include <assert.h>
#include <ctype.h>
#include <liburing.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "util.h"

#define HTTP_NEWLINE "\r\n"

static const bool HTTP_RESPONSE_CLOSE = true;
static const bool HTTP_RESPONSE_ALLOW = false;

static bool http_response_close_submit(struct Connection*, struct io_uring*);
static bool http_response_error_submit(struct Connection*, struct io_uring*,
                                       enum HttpStatus, bool close);

#define _METHOD(M, N) { M, N },
static const struct {
    enum HttpMethod method;
    const char*     name;
} HTTP_METHOD_NAMES[] = { HTTP_METHOD_ENUM{ 0, "" } };
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
} HTTP_VERSION_STRINGS[] = { HTTP_VERSION_ENUM{ 0, NULL } };
#undef _VERSION

static const char* http_version_string(enum HttpVersion version) {
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

    return HTTP_VERSION_INVALID;
}

static const char* http_status_reason(enum HttpStatus status) {
#define _STATUS(CODE, TYPE, REASON) { CODE, REASON },
    static const struct {
        enum HttpStatus status;
        const char*     reason;
    } HTTP_STATUS_REASONS[] = { HTTP_STATUS_ENUM{ 0, NULL } };
#undef STATUS

    for (size_t i = 0;
         i < sizeof(HTTP_STATUS_REASONS) / sizeof(HTTP_STATUS_REASONS[0]);
         i++) {
        if (HTTP_STATUS_REASONS[i].status == status)
            return HTTP_STATUS_REASONS[i].reason;
    }

    return NULL;
}

#define _CTYPE(T, S) { T, S },
static const struct {
    enum HttpContentType type;
    const char*          str;
} HTTP_CONTENT_TYPE_NAMES[] = { HTTP_CONTENT_TYPE_ENUM{ 0, NULL } };
#undef _CTYPE

static const char* http_content_type_name(enum HttpContentType type) {
    for (size_t i = 0; i < sizeof(HTTP_CONTENT_TYPE_NAMES) /
                               sizeof(HTTP_CONTENT_TYPE_NAMES[0]);
         i++) {
        if (HTTP_CONTENT_TYPE_NAMES[i].type == type)
            return HTTP_CONTENT_TYPE_NAMES[i].str;
    }

    return NULL;
}

// Try to parse as much of the first line of the HTTP request as possible.
// Returns:
//   -1 on error.
//    0 for more data.
//    1 on completion.
//    2 to bail successfully (for HTTP errors, which are "successful" from the
//    perspective of a connection).
static int8_t http_request_parse_first_line(struct Connection* conn,
                                            struct io_uring*   uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;
    struct Buffer* buf       = &conn->recv_buf;

    this->version      = HTTP_VERSION_11;
    this->keep_alive   = true;
    this->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    // If no CRLF has appeared so far, and the length of the data is
    // permissible, bail and wait for more.
    if (!buf_memmem(buf, HTTP_NEWLINE)) {
        if (buf_len(buf) < HTTP_REQUEST_LINE_MAX_LENGTH)
            return 0;
        return http_response_error_submit(conn, uring, HTTP_STATUS_URI_TOO_LONG,
                                          HTTP_RESPONSE_CLOSE);
    }

    this->method = http_request_method_parse(buf_token_next(buf, " "));
    switch (this->method) {
    case HTTP_METHOD_INVALID:
        log_msg(TRACE, "Got an invalid method.");
        return http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                          HTTP_RESPONSE_CLOSE)
                   ? 2
                   : -1;
    case HTTP_METHOD_UNKNOWN:
        log_msg(TRACE, "Got an unknown method.");
        return http_response_error_submit(conn, uring,
                                          HTTP_STATUS_NOT_IMPLEMENTED,
                                          HTTP_RESPONSE_ALLOW)
                   ? 2
                   : -1;
    default:
        break;
    }

    this->version = http_version_parse(buf_token_next(buf, " "));
    if (this->version == HTTP_VERSION_INVALID) {
        log_msg(TRACE, "Got a bad HTTP version.");
        return http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                          HTTP_RESPONSE_CLOSE)
                   ? 2
                   : -1;
    }

    if (!buf_consume(buf, HTTP_NEWLINE))
        return http_response_error_submit(conn, uring, HTTP_STATUS_BAD_REQUEST,
                                          HTTP_RESPONSE_CLOSE)
                   ? 2
                   : -1;

    PANIC("TODO: Parse rest of first line.");
}

// Try to parse as much of the HTTP request as possible.
// Returns:
//   -1 on error.
//    0 for more data.
//    1 on completion.
int8_t http_request_handle(struct Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;
    int8_t rc                = -1;

    switch (this->state) {
    case REQUEST_INIT:
        rc = http_request_parse_first_line(conn, uring);
        if (rc == -1 || rc == 0)
            return rc;
        else if (rc == 2)
            return 1;
        break;
    case REQUEST_CLOSING:
        return 1;
    default:
        break;
    }

    PANIC("TODO");
}

// Respond to a completed send event.
bool http_response_handle(struct Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;

    // Send isn't done.
    if (buf_len(&conn->send_buf) > 0)
        return true;

    switch (this->state) {
    case REQUEST_RESPONDING:
        if (this->keep_alive) {
            connection_reset(conn);
            this->state = REQUEST_INIT;
            return conn->recv_submit(conn, uring, 0);
        }

        return http_response_close_submit(conn, uring);
    case REQUEST_CLOSING:
        return true;
    default:
        PANIC_FMT("Invalid state in response_handle: %d.", this->state);
    }
}

// Write the status line to the send buffer.
static bool http_response_prep_status_line(struct Connection* conn,
                                           enum HttpStatus    status) {
    assert(conn);
    assert(status != HTTP_STATUS_INVALID);

    struct HttpRequest* req = &conn->request;
    struct Buffer*      buf = &conn->send_buf;

    if (!buf_initialized(buf) && !connection_send_buf_init(conn)) {
        log_msg(WARN, "Failed to initialize send buffer.");
        return false;
    }

    TRYB(buf_write_str(buf, http_version_string(req->version)));
    TRYB(buf_write_byte(buf, ' '));
    TRYB(buf_write_num(buf, status));
    TRYB(buf_write_byte(buf, ' '));

    const char* reason = http_status_reason(status);
    if (!reason) {
        log_fmt(WARN, "Invalid HTTP status %d.", status);
        return false;
    }
    TRYB(buf_write_str(buf, reason));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));

    return true;
}

static bool http_response_prep_header(struct Connection* conn, const char* name,
                                      const char* value) {
    assert(conn);
    assert(name);
    assert(value);

    struct Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, ": "));
    TRYB(buf_write_str(buf, value));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));
    return true;
}

static bool http_response_prep_header_num(struct Connection* conn,
                                          const char* name, ssize_t value) {
    assert(conn);
    assert(name);
    assert(value);

    struct Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, ": "));
    TRYB(buf_write_num(buf, value));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));
    return true;
}

static bool http_response_prep_date_header(struct Connection* conn) {
    assert(conn);

    struct Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, "Date: "));

    time_t current_time = time(NULL);
    size_t written =
        strftime((char*)buf_write_ptr(buf), buf_space(buf),
                 "%a, %d %b %Y %H:%M:%S GMT", gmtime(&current_time));
    TRYB(written);
    buf_wrote(buf, written);

    TRYB(buf_write_str(buf, HTTP_NEWLINE));

    return true;
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_headers(struct Connection* conn,
                                       enum HttpStatus    status,
                                       ssize_t content_length, bool close) {
    assert(conn);

    struct HttpRequest* this = &conn->request;

    TRYB(http_response_prep_status_line(conn, status));
    TRYB(http_response_prep_date_header(conn));
    if (close || !this->keep_alive || content_length < 0) {
        this->keep_alive = false;
        TRYB(http_response_prep_header(conn, "Connection", "Close"));
    } else {
        TRYB(http_response_prep_header(conn, "Connection", "Keep-Alive"));
    }
    if (content_length >= 0)
        TRYB(http_response_prep_header_num(conn, "Content-Length",
                                           content_length));
    TRYB(http_response_prep_header(conn, "Content-Type",
                                   http_content_type_name(this->content_type)));

    return true;
}

// Done writing headers.
static bool http_response_prep_headers_done(struct Connection* conn) {
    assert(conn);

    return buf_write_str(&conn->send_buf, HTTP_NEWLINE);
}

// Write out a response body to the send buffer.
static bool http_response_prep_body(struct Connection* conn, const char* body) {
    assert(conn);

    return buf_write_str(&conn->send_buf, body);
}

static const char* http_response_error_make_body(struct Connection* conn,
                                                 enum HttpStatus    status) {
    assert(conn);
    assert(status != HTTP_STATUS_INVALID);

    static char body[HTTP_ERROR_BODY_MAX_LENGTH] = { '\0' };

    // TODO: De-uglify. Probably should load a template from somewhere.
    if (snprintf(body, HTTP_ERROR_BODY_MAX_LENGTH,
                 "<!DOCTYPE html>\n"
                 "<html>\n"
                 "<head>\n"
                 "<title>Error: %d</title>\n"
                 "</head>\n"
                 "<body>\n"
                 "<h1>HTTP Error %d</h1>\n"
                 "<p>%s.</p>\n"
                 "</body>\n"
                 "</html>\n",
                 status, status,
                 http_status_reason(status)) > HTTP_ERROR_BODY_MAX_LENGTH)
        return NULL;

    return body;
}

static bool http_response_close_submit(struct Connection* conn,
                                       struct io_uring*   uring) {
    assert(conn);
    assert(uring);

    conn->request.state = REQUEST_CLOSING;
    return connection_close_submit(conn, uring);
}

// Submit a write event for an HTTP error response.
static bool http_response_error_submit(struct Connection* conn,
                                       struct io_uring*   uring,
                                       enum HttpStatus status, bool close) {
    assert(conn);
    assert(uring);
    assert(status != HTTP_STATUS_INVALID);

    struct HttpRequest* this = &conn->request;

    this->state        = REQUEST_RESPONDING;
    this->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    const char* body = http_response_error_make_body(conn, status);
    TRYB(body);

    TRYB(http_response_prep_headers(
        conn, status, strnlen(body, HTTP_ERROR_BODY_MAX_LENGTH), close));
    TRYB(http_response_prep_headers_done(conn));

    TRYB(http_response_prep_body(conn, body));

    TRYB(conn->send_submit(conn, uring, close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_response_close_submit(conn, uring);

    return true;
}
