#include "http_response.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing/io_uring.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "event.h"
#include "forward.h"
#include "http_connection.h"
#include "http_parse.h"
#include "http_types.h"
#include "log.h"
#include "ptr.h"
#include "ptr_util.h"
#include "socket.h"
#include "util.h"

static bool http_response_close_submit(HttpConnection*, struct io_uring*);

// Respond to a completed send event.
bool http_response_handle(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    // Send isn't done.
    if (buf_len(&this->conn.send_buf) > 0)
        return true;

    switch (this->state) {
    case CONNECTION_RESPONDING:
        if (this->keep_alive) {
            http_connection_reset(this);
            this->state = CONNECTION_INIT;
            return this->conn.recv_submit(&this->conn, uring, 0);
        }

        return http_response_close_submit(this, uring);
    case CONNECTION_CLOSING:
        return true;
    default:
        PANIC_FMT("Invalid state in response_handle: %d.", this->state);
    }
}

// Write the status line to the send buffer.
static bool http_response_prep_status_line(HttpConnection* this,
                                           HttpStatus status) {
    assert(this);
    assert(status != HTTP_STATUS_INVALID);

    Buffer* buf = &this->conn.send_buf;

    TRYB(buf_write_str(buf, http_version_string(this->version)));
    TRYB(buf_write_byte(buf, ' '));
    TRYB(buf_write_num(buf, status));
    TRYB(buf_write_byte(buf, ' '));

    CString reason = http_status_reason(status);
    if (!reason.ptr) {
        log_fmt(WARN, "Invalid HTTP status %d.", status);
        return false;
    }
    TRYB(buf_write_str(buf, reason));
    return buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header(HttpConnection* this, CString name,
                                      CString value) {
    assert(this);
    assert(name.ptr);
    assert(value.ptr);

    Buffer* buf = &this->conn.send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_str(buf, value));
    return buf_write_str(buf, HTTP_NEWLINE);
}

FORMAT_FN(3, 4)
static bool http_response_prep_header_fmt(HttpConnection* this, CString name,
                                          const char* fmt, ...) {
    assert(this);
    assert(name.ptr);
    assert(fmt);

    Buffer* buf = &this->conn.send_buf;
    va_list args;
    va_start(args, fmt);

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    bool ret = buf_write_vfmt(buf, fmt, args);
    ret      = ret && buf_write_str(buf, HTTP_NEWLINE);

    va_end(args);
    return ret;
}

static bool http_response_prep_header_num(HttpConnection* this, CString name,
                                          ssize_t value) {
    assert(this);
    assert(name.ptr);

    Buffer* buf = &this->conn.send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_num(buf, value));
    return buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header_date(HttpConnection* this, CString name,
                                           time_t tv) {
    assert(this);

    static struct {
        time_t tv;
        char   buf[HTTP_DATE_BUF_LENGTH];
        size_t len;
    } DATES[HTTP_DATE_CACHE] = { { 0, { '\0' }, 0 } };

    size_t i = tv % HTTP_DATE_CACHE;
    if (DATES[i].tv != tv)
        UNWRAPN(DATES[i].len,
                strftime(DATES[i].buf, HTTP_DATE_BUF_LENGTH,
                         "%a, %d %b %Y %H:%M:%S GMT", gmtime(&tv)));

    return http_response_prep_header(
        this, name, (CString){ .ptr = DATES[i].buf, .len = DATES[i].len });
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_default_headers(HttpConnection* this,
                                               HttpStatus status,
                                               ssize_t    content_length,
                                               bool       close) {
    assert(this);

    TRYB(http_response_prep_status_line(this, status));
    TRYB(http_response_prep_header_date(this, CS("Date"), time(NULL)));
    if (close || !this->keep_alive || content_length < 0) {
        this->keep_alive = false;
        TRYB(http_response_prep_header(this, CS("Connection"), CS("Close")));
    } else {
        TRYB(http_response_prep_header(this, CS("Connection"),
                                       CS("Keep-Alive")));
    }
    if (content_length >= 0)
        TRYB(http_response_prep_header_num(this, CS("Content-Length"),
                                           content_length));
    TRYB(http_response_prep_header(
        this, CS("Content-Type"),
        http_content_type_name(this->response_content_type)));

    return true;
}

// Done writing headers.
static bool http_response_prep_headers_done(HttpConnection* this) {
    assert(this);

    return buf_write_str(&this->conn.send_buf, HTTP_NEWLINE);
}

// Write out a response body to the send buffer.
static bool http_response_prep_body(HttpConnection* this, CString body) {
    assert(this);

    return buf_write_str(&this->conn.send_buf, body);
}

static CString http_response_error_make_body(HttpConnection* this,
                                             HttpStatus status) {
    assert(this);
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
                 status, http_version_string(this->version).ptr, status,
                 http_status_reason(status).ptr) > HTTP_ERROR_BODY_MAX_LENGTH)
        return CS_NULL;

    return (CString){ .ptr = body,
                      .len = strnlen(body, HTTP_ERROR_BODY_MAX_LENGTH) };
}

static bool http_response_close_submit(HttpConnection* this,
                                       struct io_uring* uring) {
    assert(this);
    assert(uring);

    this->state = CONNECTION_CLOSING;
    return connection_close_submit(&this->conn, uring);
}

// Submit a write event for an HTTP error response.
bool http_response_error_submit(HttpConnection* this, struct io_uring* uring,
                                HttpStatus status, bool close) {
    assert(this);
    assert(uring);
    assert(status != HTTP_STATUS_INVALID);

    log_fmt(DEBUG, "HTTP error %d. %s", status,
            close ? "Closing connection." : "");

    // Clear any previously written data.
    buf_reset(&this->conn.send_buf);

    this->state                 = CONNECTION_RESPONDING;
    this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    CString body = http_response_error_make_body(this, status);
    TRYB(body.ptr);

    TRYB(http_response_prep_default_headers(this, status, body.len, close));
    TRYB(http_response_prep_headers_done(this));

    if (this->method != HTTP_METHOD_HEAD)
        TRYB(http_response_prep_body(this, body));

    TRYB(this->conn.send_submit(&this->conn, uring, close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_response_close_submit(this, uring);

    return true;
}

bool http_response_file_submit(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    this->state = CONNECTION_RESPONDING;

    assert(this->target_file >= 0);

    struct stat res;
    TRYB(fstat(this->target_file, &res) == 0);

    bool index = false;

    // TODO: Directory listings.
    if (S_ISDIR(res.st_mode)) {
        index    = true;
        fd dirfd = this->target_file;
        // TODO: Directory listings.
        if (fstatat(dirfd, INDEX_FILENAME, &res, 0) != 0)
            return http_response_error_submit(
                this, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);
        fd new_file = -1;
        if ((new_file = openat(dirfd, INDEX_FILENAME, O_RDONLY)) < 0)
            return http_response_error_submit(
                this, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);
        close(dirfd);
        this->target_file = new_file;
    }

    if (!S_ISREG(res.st_mode))
        return http_response_error_submit(this, uring, HTTP_STATUS_NOT_FOUND,
                                          HTTP_RESPONSE_ALLOW);

    if (index)
        this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;
    else
        this->response_content_type =
            http_content_type_from_path(S_CONST(this->target_path));

    TRYB(http_response_prep_default_headers(this, HTTP_STATUS_OK, res.st_size,
                                            HTTP_RESPONSE_ALLOW));
    TRYB(http_response_prep_header_date(this, CS("Last-Modified"),
                                        res.st_mtim.tv_sec));
    TRYB(http_response_prep_header_fmt(this, CS("Etag"), "\"%luX%lX%lX\"",
                                       res.st_ino, res.st_mtim.tv_sec,
                                       res.st_size));
    TRYB(http_response_prep_headers_done(this));

    Buffer* buf = &this->conn.send_buf;
    if (this->method == HTTP_METHOD_HEAD) {
        TRYB(this->conn.send_submit(&this->conn, uring,
                                    !this->keep_alive ? IOSQE_IO_LINK : 0));
    } else {
        size_t file_size   = (size_t)res.st_size;
        size_t header_size = buf_len(buf);
        size_t total_size  = file_size + header_size;
        if (file_size > buf_space(buf) && !buf_ensure_cap(buf, total_size))
            buf_ensure_max_cap(buf);

        size_t sent      = 0;
        size_t file_sent = 0;
        size_t last_sent = 0;
        while (total_size > 0) {
            ByteString write_ptr     = buf_write_ptr(buf);
            size_t     try_read_size = MIN(file_size, write_ptr.len);
            TRYB(event_read_submit(&this->conn.last_event, uring,
                                   this->target_file, write_ptr, try_read_size,
                                   file_sent, IOSQE_IO_LINK));
            buf_wrote(buf, try_read_size);
            file_size -= try_read_size;
            file_sent += try_read_size;
            last_sent = buf_len(buf);
            TRYB(this->conn.send_submit(
                &this->conn, uring,
                (total_size - last_sent > 0 || !this->keep_alive)
                    ? IOSQE_IO_LINK
                    : 0));
            total_size -= last_sent;
            sent += last_sent;
            buf_reset(buf);
        }
        buf_wrote(buf, last_sent);
    }

    if (!this->keep_alive)
        return http_response_close_submit(this, uring);

    return true;
}
