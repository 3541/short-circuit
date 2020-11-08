#include "http_response.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <stdbool.h>
#include <sys/param.h>
#include <unistd.h>

#include "config.h"
#include "connection.h"
#include "http_parse.h"
#include "ptr.h"
#include "ptr_util.h"

static bool http_response_close_submit(Connection*, struct io_uring*);

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
                 "<h1>%s Error %d</h1>\n"
                 "<p>%s.</p>\n"
                 "</body>\n"
                 "</html>\n",
                 status, http_version_string(conn->request.version).ptr, status,
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

    // Clear any previously written data.
    if (buf_initialized(&conn->send_buf))
        buf_reset(&conn->send_buf);

    this->state                 = REQUEST_RESPONDING;
    this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    CString body = http_response_error_make_body(conn, status);
    TRYB(body.ptr);

    TRYB(http_response_prep_headers(conn, status, body.len, close));
    TRYB(http_response_prep_headers_done(conn));

    if (this->method != HTTP_METHOD_HEAD)
        TRYB(http_response_prep_body(conn, body));

    TRYB(conn->send_submit(conn, uring, close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_response_close_submit(conn, uring);

    return true;
}

bool http_response_file_submit(Connection*      conn,
                                      struct io_uring* uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;
    this->state              = REQUEST_RESPONDING;

    assert(this->target_file >= 0);

    struct stat res;
    TRYB(fstat(this->target_file, &res) == 0);

    bool index = false;

    // TODO: Directory listings.
    if (S_ISDIR(res.st_mode)) {
        index    = true;
        fd dirfd = this->target_file;
        TRYB(fstatat(dirfd, INDEX_FILENAME, &res, 0) == 0);
        fd new_file = -1;
        if ((new_file = openat(dirfd, INDEX_FILENAME, O_RDONLY)) < 0)
            return http_response_error_submit(
                conn, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);
        close(dirfd);
        this->target_file = new_file;
    }

    if (!S_ISREG(res.st_mode))
        return http_response_error_submit(conn, uring, HTTP_STATUS_NOT_FOUND,
                                          HTTP_RESPONSE_ALLOW);

    if (index)
        this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;
    else
        this->response_content_type =
            http_content_type_from_path(S_CONST(this->target_path));

    TRYB(http_response_prep_headers(conn, HTTP_STATUS_OK, res.st_size,
                                    HTTP_RESPONSE_ALLOW));
    TRYB(http_response_prep_headers_done(conn));

    Buffer* buf     = &conn->send_buf;
    if (this->method == HTTP_METHOD_HEAD) {
        TRYB(conn->send_submit(conn, uring,
                               !this->keep_alive ? IOSQE_IO_LINK : 0));
    } else {
        size_t file_size = (size_t)res.st_size;
        size_t header_size = buf_len(buf);
        size_t total_size = file_size + header_size;
        if (file_size > buf_space(buf) && !buf_ensure_cap(buf, total_size))
            buf_ensure_max_cap(buf);

        size_t sent = 0;
        size_t file_sent = 0;
        while (total_size > 0) {
            ByteString write_ptr = buf_write_ptr(buf);
            size_t try_read_size = MIN(file_size, write_ptr.len);
            TRYB(event_read_submit(&conn->last_event, uring, this->target_file,
                                   write_ptr, try_read_size, file_sent, IOSQE_IO_LINK));
            buf_wrote(buf, try_read_size);
            file_size -= try_read_size;
            file_sent += try_read_size;
            TRYB(conn->send_submit(
                conn, uring,
                (total_size - buf_len(buf) > 0 || !this->keep_alive) ? IOSQE_IO_LINK : 0));
            total_size -= buf_len(buf);
            sent += buf_len(buf);
            buf_reset(buf);
        }
        buf_wrote(buf, MIN(sent, buf_space(buf)));
    }

    if (!this->keep_alive)
        return http_response_close_submit(conn, uring);

    return true;
}
