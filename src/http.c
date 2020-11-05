#include "http.h"

#include <assert.h>
#include <liburing.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "http_parse.h"
#include "ptr.h"
#include "ptr_util.h"
#include "uri.h"
#include "util.h"

static bool http_response_close_submit(Connection*, struct io_uring*);

static void http_request_init(HttpRequest* this) {
    assert(this);

    this->state                       = REQUEST_INIT;
    this->version                     = HTTP_VERSION_11;
    this->keep_alive                  = true;
    this->content_length              = -1;
    this->transfer_encodings          = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;
}

void http_request_reset(HttpRequest* this) {
    assert(this);

    if (this->host.ptr)
        string_free(CS_MUT(this->host));

    if (uri_is_initialized(&this->target))
        uri_free(&this->target);

    memset(this, 0, sizeof(HttpRequest));
}

// Do whatever is appropriate for the parsed method.
// static HttpRequestStateResult http_request_method_handle(Connection* conn,
// struct io_uring* uring);

// Try to parse as much of the HTTP request as possible.
HttpRequestResult http_request_handle(Connection*      conn,
                                      struct io_uring* uring) {
    assert(conn);
    assert(uring);

    HttpRequest* this         = &conn->request;
    HttpRequestStateResult rc = HTTP_REQUEST_STATE_ERROR;

    // Go through as many states as possible with the data currently loaded.
    switch (this->state) {
    case REQUEST_INIT:
        http_request_init(this);
        if ((rc = http_request_first_line_parse(conn, uring) !=
                  HTTP_REQUEST_STATE_DONE))
            return (HttpRequestResult)rc;
        // fallthrough
    case REQUEST_PARSED_FIRST_LINE:
        if ((rc = http_request_headers_parse(conn, uring) !=
                  HTTP_REQUEST_STATE_DONE))
            return (HttpRequestResult)rc;
        // fallthrough
    case REQUEST_PARSED_HEADERS:
        PANIC("TODO: Handle REQUEST_PARSED_HEADERS.");
        break;
    case REQUEST_RESPONDING:
        PANIC("TODO: Handle REQUEST_RESPONDING.");
        break;
    case REQUEST_CLOSING:
        return 1;
    }

    log_fmt(TRACE, "State: %d", this->state);
    PANIC("TODO: Handle whatever request did this.");
}

// Respond to a completed send event.
bool http_response_handle(Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    HttpRequest* this = &conn->request;

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
static bool http_response_prep_status_line(Connection* conn,
                                           HttpStatus  status) {
    assert(conn);
    assert(status != HTTP_STATUS_INVALID);

    HttpRequest* req = &conn->request;
    Buffer*      buf = &conn->send_buf;

    if (!buf_initialized(buf) && !connection_send_buf_init(conn)) {
        log_msg(WARN, "Failed to initialize send buffer.");
        return false;
    }

    TRYB(buf_write_str(buf, http_version_string(req->version)));
    TRYB(buf_write_byte(buf, ' '));
    TRYB(buf_write_num(buf, status));
    TRYB(buf_write_byte(buf, ' '));

    CString reason = http_status_reason(status);
    if (!reason.ptr) {
        log_fmt(WARN, "Invalid HTTP status %d.", status);
        return false;
    }
    TRYB(buf_write_str(buf, reason));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));

    return true;
}

static bool http_response_prep_header(Connection* conn, CString name,
                                      CString value) {
    assert(conn);
    assert(name.ptr);
    assert(value.ptr);

    Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_str(buf, value));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));
    return true;
}

static bool http_response_prep_header_num(Connection* conn, CString name,
                                          ssize_t value) {
    assert(conn);
    assert(name.ptr);

    Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_num(buf, value));
    TRYB(buf_write_str(buf, HTTP_NEWLINE));
    return true;
}

static bool http_response_prep_date_header(Connection* conn) {
    assert(conn);

    Buffer* buf = &conn->send_buf;

    TRYB(buf_write_str(buf, CS("Date: ")));

    time_t current_time = time(NULL);
    String write_ptr    = buf_write_ptr_string(buf);
    size_t written =
        strftime(write_ptr.ptr, write_ptr.len, "%a, %d %b %Y %H:%M:%S GMT",
                 gmtime(&current_time));
    TRYB(written);
    buf_wrote(buf, written);

    TRYB(buf_write_str(buf, HTTP_NEWLINE));

    return true;
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_headers(Connection* conn, HttpStatus status,
                                       ssize_t content_length, bool close) {
    assert(conn);

    HttpRequest* this = &conn->request;

    TRYB(http_response_prep_status_line(conn, status));
    TRYB(http_response_prep_date_header(conn));
    if (close || !this->keep_alive || content_length < 0) {
        this->keep_alive = false;
        TRYB(http_response_prep_header(conn, CS("Connection"), CS("Close")));
    } else {
        TRYB(http_response_prep_header(conn, CS("Connection"),
                                       CS("Keep-Alive")));
    }
    if (content_length >= 0)
        TRYB(http_response_prep_header_num(conn, CS("Content-Length"),
                                           content_length));
    TRYB(http_response_prep_header(
        conn, CS("Content-Type"),
        http_content_type_name(this->response_content_type)));

    return true;
}

// Done writing headers.
static bool http_response_prep_headers_done(Connection* conn) {
    assert(conn);

    return buf_write_str(&conn->send_buf, HTTP_NEWLINE);
}

// Write out a response body to the send buffer.
static bool http_response_prep_body(Connection* conn, CString body) {
    assert(conn);

    return buf_write_str(&conn->send_buf, body);
}

static CString http_response_error_make_body(Connection* conn,
                                             HttpStatus  status) {
    assert(conn);
    (void)conn;
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
                 http_status_reason(status).ptr) > HTTP_ERROR_BODY_MAX_LENGTH)
        return CS_NULL;

    return (CString){ .ptr = body,
                      .len = strnlen(body, HTTP_ERROR_BODY_MAX_LENGTH) };
}

static bool http_response_close_submit(Connection*      conn,
                                       struct io_uring* uring) {
    assert(conn);
    assert(uring);

    conn->request.state = REQUEST_CLOSING;
    return connection_close_submit(conn, uring);
}

// Submit a write event for an HTTP error response.
bool http_response_error_submit(Connection* conn, struct io_uring* uring,
                                HttpStatus status, bool close) {
    assert(conn);
    assert(uring);
    assert(status != HTTP_STATUS_INVALID);

    log_fmt(DEBUG, "HTTP error %d. %s", status,
            close ? "Closing connection." : "");

    HttpRequest* this = &conn->request;

    this->state                 = REQUEST_RESPONDING;
    this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    CString body = http_response_error_make_body(conn, status);
    TRYB(body.ptr);

    TRYB(http_response_prep_headers(conn, status, body.len, close));
    TRYB(http_response_prep_headers_done(conn));

    TRYB(http_response_prep_body(conn, body));

    TRYB(conn->send_submit(conn, uring, close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_response_close_submit(conn, uring);

    return true;
}
