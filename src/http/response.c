/*
 * SHORT CIRCUIT: HTTP RESPONSE -- HTTP response submission.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

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
#include "file.h"
#include "http/connection.h"
#include "http/types.h"

static inline HttpConnection* http_response_connection(HttpResponse* resp) {
    assert(resp);

    return CONTAINER_OF(resp, HttpConnection, response);
}

static inline Buffer* http_response_send_buf(HttpResponse* resp) {
    return &http_response_connection(resp)->conn.send_buf;
}

void http_response_init(HttpResponse* resp) {
    assert(resp);

    resp->content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;
    resp->transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
}

void http_response_reset(HttpResponse* resp) {
    assert(resp);

    http_response_init(resp);
}

// Respond to a mid-response event.
bool http_response_handle(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    // Send isn't done.
    if (buf_len(&this->conn.send_buf) > 0)
        return true;

    switch (this->state) {
    case CONNECTION_OPENING_FILE:
        return http_response_file_submit(&this->response, uring);
    case CONNECTION_RESPONDING:
        if (this->keep_alive) {
            TRYB(http_connection_reset(this, uring));
            this->state = CONNECTION_INIT;
            return this->conn.recv_submit(&this->conn, uring, 0, 0);
        }

        return http_connection_close_submit(this, uring);
    case CONNECTION_CLOSING:
        return true;
    default:
        PANIC_FMT("Invalid state in response_handle: %d.", this->state);
    }
}

// Write the status line to the send buffer.
static bool http_response_prep_status_line(HttpResponse* resp,
                                           HttpStatus    status) {
    assert(resp);
    assert(status != HTTP_STATUS_INVALID);

    Buffer*  buf         = http_response_send_buf(resp);
    uint16_t status_code = http_status_code(status);

    TRYB(buf_write_str(
        buf, http_version_string(http_response_connection(resp)->version)));
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

static bool http_response_prep_header(HttpResponse* resp, CString name,
                                      CString value) {
    assert(resp);
    assert(name.ptr);
    assert(value.ptr);

    Buffer* buf = http_response_send_buf(resp);

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_str(buf, value));
    return buf_write_str(buf, HTTP_NEWLINE);
}

FORMAT_FN(3, 4)
static bool http_response_prep_header_fmt(HttpResponse* resp, CString name,
                                          const char* fmt, ...) {
    assert(resp);
    assert(name.ptr);
    assert(fmt);

    Buffer* buf = http_response_send_buf(resp);
    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));

    va_list args;
    va_start(args, fmt);

    bool ret = buf_write_vfmt(buf, fmt, args);
    ret      = ret && buf_write_str(buf, HTTP_NEWLINE);

    va_end(args);
    return ret;
}

static bool http_response_prep_header_num(HttpResponse* resp, CString name,
                                          size_t value) {
    assert(resp);
    assert(name.ptr);

    Buffer* buf = http_response_send_buf(resp);

    TRYB(buf_write_str(buf, name));
    TRYB(buf_write_str(buf, CS(": ")));
    TRYB(buf_write_num(buf, value));
    return buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header_timestamp(HttpResponse* resp,
                                                CString name, time_t tv) {
    assert(resp);

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
        resp, name, (CString) { .ptr = TIMES[i].buf, .len = TIMES[i].len });
}

static bool http_response_prep_header_date(HttpResponse* resp) {
    assert(resp);

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

    return http_response_prep_header(resp, CS("Date"), DATE);
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_default_headers(HttpResponse* resp,
                                               HttpStatus    status,
                                               ssize_t       content_length,
                                               bool          close) {
    assert(resp);

    HttpConnection* conn = http_response_connection(resp);

    TRYB(http_response_prep_status_line(resp, status));
    TRYB(http_response_prep_header_date(resp));
    if (close || !conn->keep_alive || content_length < 0) {
        conn->keep_alive = false;
        TRYB(http_response_prep_header(resp, CS("Connection"), CS("Close")));
    } else {
        TRYB(http_response_prep_header(resp, CS("Connection"),
                                       CS("Keep-Alive")));
    }
    if (content_length >= 0)
        TRYB(http_response_prep_header_num(resp, CS("Content-Length"),
                                           (size_t)content_length));
    TRYB(http_response_prep_header(resp, CS("Content-Type"),
                                   http_content_type_name(resp->content_type)));

    return true;
}

// Done writing headers.
static bool http_response_prep_headers_done(HttpResponse* resp) {
    assert(resp);

    return buf_write_str(http_response_send_buf(resp), HTTP_NEWLINE);
}

// Write out a response body to the send buffer.
static bool http_response_prep_body(HttpResponse* resp, CString body) {
    assert(resp);

    return buf_write_str(http_response_send_buf(resp), body);
}

static CString http_response_error_make_body(HttpResponse* resp,
                                             HttpStatus    status) {
    assert(resp);
    assert(status != HTTP_STATUS_INVALID);

    static uint8_t  body[HTTP_ERROR_BODY_MAX_LENGTH] = { '\0' };
    ssize_t         len                              = 0;
    uint16_t        status_code                      = http_status_code(status);
    HttpConnection* conn = http_response_connection(resp);

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
                        status_code, http_version_string(conn->version).ptr,
                        status_code, http_status_reason(status).ptr)) >
            HTTP_ERROR_BODY_MAX_LENGTH ||
        len < 0)
        return CS_NULL;

    return (CString) { .ptr = body, .len = (size_t)len };
}

// Submit a write event for an HTTP error response.
bool http_response_error_submit(HttpResponse* resp, struct io_uring* uring,
                                HttpStatus status, bool close) {
    assert(resp);
    assert(uring);
    assert(status != HTTP_STATUS_INVALID);

    log_fmt(DEBUG, "HTTP error %d. %s", http_status_code(status),
            close ? "Closing connection." : "");

    // Clear any previously written data.
    buf_reset(http_response_send_buf(resp));

    HttpConnection* conn = http_response_connection(resp);

    conn->state        = CONNECTION_RESPONDING;
    resp->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;

    CString body = http_response_error_make_body(resp, status);
    TRYB(body.ptr);

    TRYB(http_response_prep_default_headers(resp, status, (ssize_t)body.len,
                                            close));
    TRYB(http_response_prep_headers_done(resp));

    if (conn->method != HTTP_METHOD_HEAD)
        TRYB(http_response_prep_body(resp, body));

    TRYB(conn->conn.send_submit(&conn->conn, uring, 0,
                                close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_connection_close_submit(conn, uring);

    return true;
}

bool http_response_file_submit(HttpResponse* resp, struct io_uring* uring) {
    assert(resp);
    assert(uring);

    HttpConnection* conn = http_response_connection(resp);

    if (!conn->target_file) {
        conn->state = CONNECTION_OPENING_FILE;
        conn->target_file =
            file_open(uring, S_CONST(conn->request.target_path), O_RDONLY);
    }

    if (!conn->target_file)
        return http_response_error_submit(resp, uring, HTTP_STATUS_NOT_FOUND,
                                          HTTP_RESPONSE_ALLOW);

    conn->state = CONNECTION_RESPONDING;

    fd target_file = file_handle_fd(conn->target_file);
    assert(target_file >= 0);

    struct stat res;
    TRYB(fstat(target_file, &res) == 0);

    bool index = false;

    if (S_ISDIR(res.st_mode)) {
        FileHandle* index_file =
            file_openat(uring, conn->target_file, INDEX_FILENAME, O_RDONLY);
        // TODO: Directory listings.
        if (!index_file)
            return http_response_error_submit(
                resp, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);

        file_close(conn->target_file, uring);
        conn->target_file = index_file;
        target_file       = file_handle_fd(conn->target_file);
        TRYB(fstat(target_file, &res) == 0);
    }

    if (!S_ISREG(res.st_mode))
        return http_response_error_submit(resp, uring, HTTP_STATUS_NOT_FOUND,
                                          HTTP_RESPONSE_ALLOW);
    if (index)
        resp->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;
    else
        resp->content_type =
            http_content_type_from_path(S_CONST(conn->request.target_path));

    TRYB(http_response_prep_default_headers(resp, HTTP_STATUS_OK, res.st_size,
                                            HTTP_RESPONSE_ALLOW));
    TRYB(http_response_prep_header_timestamp(resp, CS("Last-Modified"),
                                             res.st_mtim.tv_sec));
    TRYB(http_response_prep_header_fmt(resp, CS("Etag"), "\"%luX%lX%lX\"",
                                       res.st_ino, res.st_mtime, res.st_size));
    TRYB(http_response_prep_headers_done(resp));

    // Perhaps instead of just sending here, it would be better to write into
    // the same pipe that is used for splice.
    TRYB(conn->conn.send_submit(
        &conn->conn, uring, (conn->method == HTTP_METHOD_HEAD) ? 0 : MSG_MORE,
        (!conn->keep_alive || conn->method != HTTP_METHOD_HEAD) ? IOSQE_IO_LINK
                                                                : 0));
    if (conn->method == HTTP_METHOD_HEAD)
        goto done;

    // TODO: This will not work for TLS.
    TRYB(connection_splice_submit(&conn->conn, uring, target_file,
                                  (size_t)res.st_size,
                                  !conn->keep_alive ? IOSQE_IO_LINK : 0));

done:
    if (!conn->keep_alive)
        return http_connection_close_submit(conn, uring);
    return true;
}
