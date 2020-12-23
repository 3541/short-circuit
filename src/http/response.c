#include "http/response.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "http/connection.h"
#include "http/types.h"
#include "socket.h"

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
            TRYB(http_connection_reset(this, uring));
            this->state = CONNECTION_INIT;
            return this->conn.recv_submit(&this->conn, uring, 0, 0);
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

    Buffer*  buf         = &this->conn.send_buf;
    uint16_t status_code = http_status_code(status);

    TRYB(buf_write_str(buf, http_version_string(this->version)));
    TRYB(buf_write_byte(buf, ' '));
    TRYB(buf_write_num(buf, status_code));
    TRYB(buf_write_byte(buf, ' '));

    CString reason = http_status_reason(status);
    if (!reason.ptr) {
        log_fmt(WARN, "Invalid HTTP status %d.", status_code);
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
                                          size_t value) {
    assert(this);
    assert(name.ptr);

    Buffer* buf = &this->conn.send_buf;

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_num(buf, value));
    return buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header_timestamp(HttpConnection* this,
                                                CString name, time_t tv) {
    assert(this);

    static THREAD_LOCAL struct {
        time_t  tv;
        uint8_t buf[HTTP_TIME_BUF_LENGTH];
        size_t  len;
    } TIMES[HTTP_TIME_CACHE] = { { 0, { '\0' }, 0 } };

    size_t i = (size_t)tv % HTTP_TIME_CACHE;
    if (TIMES[i].tv != tv)
        UNWRAPN(TIMES[i].len,
                strftime((char*)TIMES[i].buf, HTTP_TIME_BUF_LENGTH,
                         HTTP_TIME_FORMAT, gmtime(&tv)));

    return http_response_prep_header(
        this, name, (CString) { .ptr = TIMES[i].buf, .len = TIMES[i].len });
}

static bool http_response_prep_header_date(HttpConnection* this) {
    assert(this);

    static THREAD_LOCAL uint8_t DATE_BUF[HTTP_TIME_BUF_LENGTH] = { '\0' };
    static THREAD_LOCAL CString DATE                           = CS_NULL;
    static THREAD_LOCAL time_t  LAST_TIME                      = 0;

    time_t current = time(NULL);
    if (current - LAST_TIME > 2 || !DATE.ptr) {
        UNWRAPN(DATE.len, strftime((char*)DATE_BUF, HTTP_TIME_BUF_LENGTH,
                                   HTTP_TIME_FORMAT, gmtime(&current)));
        DATE.ptr  = DATE_BUF;
        LAST_TIME = current;
    }

    return http_response_prep_header(this, CS("Date"), DATE);
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_default_headers(HttpConnection* this,
                                               HttpStatus status,
                                               ssize_t    content_length,
                                               bool       close) {
    assert(this);

    TRYB(http_response_prep_status_line(this, status));
    TRYB(http_response_prep_header_date(this));
    if (close || !this->keep_alive || content_length < 0) {
        this->keep_alive = false;
        TRYB(http_response_prep_header(this, CS("Connection"), CS("Close")));
    } else {
        TRYB(http_response_prep_header(this, CS("Connection"),
                                       CS("Keep-Alive")));
    }
    if (content_length >= 0)
        TRYB(http_response_prep_header_num(this, CS("Content-Length"),
                                           (size_t)content_length));
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

    static uint8_t body[HTTP_ERROR_BODY_MAX_LENGTH] = { '\0' };
    ssize_t        len                              = 0;
    uint16_t       status_code                      = http_status_code(status);

    // TODO: De-uglify. Probably should load a template from somewhere.
    if ((len = snprintf((char*)body, HTTP_ERROR_BODY_MAX_LENGTH,
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
                        status_code, http_version_string(this->version).ptr,
                        status_code, http_status_reason(status).ptr)) >
            HTTP_ERROR_BODY_MAX_LENGTH ||
        len < 0)
        return CS_NULL;

    return (CString) { .ptr = body, .len = (size_t)len };
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

    log_fmt(DEBUG, "HTTP error %d. %s", http_status_code(status),
            close ? "Closing connection." : "");

    // Clear any previously written data.
    buf_reset(&this->conn.send_buf);

    this->state                 = CONNECTION_RESPONDING;
    this->response_content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    CString body = http_response_error_make_body(this, status);
    TRYB(body.ptr);

    TRYB(http_response_prep_default_headers(this, status, (ssize_t)body.len,
                                            close));
    TRYB(http_response_prep_headers_done(this));

    if (this->method != HTTP_METHOD_HEAD)
        TRYB(http_response_prep_body(this, body));

    TRYB(this->conn.send_submit(&this->conn, uring, 0,
                                close ? IOSQE_IO_LINK : 0));
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
    TRYB(http_response_prep_header_timestamp(this, CS("Last-Modified"),
                                             res.st_mtim.tv_sec));
    TRYB(http_response_prep_header_fmt(this, CS("Etag"), "\"%luX%lX%lX\"",
                                       res.st_ino, res.st_mtime, res.st_size));
    TRYB(http_response_prep_headers_done(this));

    // Perhaps instead of just sending here, it would be better to write into
    // the same pipe that is used for splice.
    TRYB(this->conn.send_submit(
        &this->conn, uring, (this->method == HTTP_METHOD_HEAD) ? 0 : MSG_MORE,
        (!this->keep_alive || this->method != HTTP_METHOD_HEAD) ? IOSQE_IO_LINK
                                                                : 0));
    if (this->method == HTTP_METHOD_HEAD)
        goto done;

    // TODO: This will not work for TLS.
    TRYB(connection_splice_submit(&this->conn, uring, this->target_file,
                                  (size_t)res.st_size,
                                  !this->keep_alive ? IOSQE_IO_LINK : 0));

done:
    if (!this->keep_alive)
        return http_response_close_submit(this, uring);
    return true;
}
