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
#include <a3/platform/types_private.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "file.h"
#include "http/connection.h"
#include "http/request.h"
#include "http/types.h"

static inline HttpConnection* http_response_connection(HttpResponse* resp) {
    assert(resp);

    return A3_CONTAINER_OF(resp, HttpConnection, response);
}

static inline A3Buffer* http_response_send_buf(HttpResponse* resp) {
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
    if (a3_buf_len(&this->conn.send_buf) > 0)
        return true;

    switch (this->state) {
    case HTTP_CONNECTION_OPENING_FILE:
        return http_response_file_submit(&this->response, uring);
    case HTTP_CONNECTION_RESPONDING:
        if (http_connection_keep_alive(this)) {
            A3_TRYB(http_connection_reset(this, uring));
            A3_TRYB(http_connection_init(this));
            return this->conn.recv_submit(&this->conn, uring, 0, 0);
        }

        return http_connection_close_submit(this, uring);
    case HTTP_CONNECTION_CLOSING:
        return true;
    default:
        A3_PANIC_FMT("Invalid state in response_handle: %d.", this->state);
    }
}

// Write the status line to the send buffer.
static bool http_response_prep_status_line(HttpResponse* resp, HttpStatus status) {
    assert(resp);
    assert(status != HTTP_STATUS_INVALID);

    A3Buffer* buf         = http_response_send_buf(resp);
    uint16_t  status_code = http_status_code(status);

    A3_TRYB(a3_buf_write_str(buf, http_version_string(http_response_connection(resp)->version)));
    A3_TRYB(a3_buf_write_byte(buf, ' '));
    A3_TRYB(a3_buf_write_num(buf, status_code));
    A3_TRYB(a3_buf_write_byte(buf, ' '));

    A3CString reason = http_status_reason(status);
    if (!reason.ptr) {
        a3_log_fmt(LOG_WARN, "Invalid HTTP status %d.", status_code);
        return false;
    }
    A3_TRYB(a3_buf_write_str(buf, reason));
    return a3_buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header(HttpResponse* resp, A3CString name, A3CString value) {
    assert(resp);
    assert(name.ptr);
    assert(value.ptr);

    A3Buffer* buf = http_response_send_buf(resp);

    A3_TRYB(a3_buf_write_str(buf, name));
    A3_TRYB(a3_buf_write_str(buf, A3_CS(": ")));
    A3_TRYB(a3_buf_write_str(buf, value));
    return a3_buf_write_str(buf, HTTP_NEWLINE);
}

A3_FORMAT_FN(3, 4)
static bool http_response_prep_header_fmt(HttpResponse* resp, A3CString name, const char* fmt,
                                          ...) {
    assert(resp);
    assert(name.ptr);
    assert(fmt);

    A3Buffer* buf = http_response_send_buf(resp);
    A3_TRYB(a3_buf_write_str(buf, name));
    A3_TRYB(a3_buf_write_str(buf, A3_CS(": ")));

    va_list args;
    va_start(args, fmt);

    bool ret = a3_buf_write_vfmt(buf, fmt, args);
    ret      = ret && a3_buf_write_str(buf, HTTP_NEWLINE);

    va_end(args);
    return ret;
}

static bool http_response_prep_header_num(HttpResponse* resp, A3CString name, size_t value) {
    assert(resp);
    assert(name.ptr);

    A3Buffer* buf = http_response_send_buf(resp);

    A3_TRYB(a3_buf_write_str(buf, name));
    A3_TRYB(a3_buf_write_str(buf, A3_CS(": ")));
    A3_TRYB(a3_buf_write_num(buf, value));
    return a3_buf_write_str(buf, HTTP_NEWLINE);
}

static bool http_response_prep_header_timestamp(HttpResponse* resp, A3CString name, time_t tv) {
    assert(resp);

    static THREAD_LOCAL struct {
        time_t  tv;
        uint8_t buf[HTTP_TIME_BUF_LENGTH];
        size_t  len;
    } TIMES[HTTP_TIME_CACHE] = { { 0, { '\0' }, 0 } };

    size_t i = (size_t)tv % HTTP_TIME_CACHE;
    if (TIMES[i].tv != tv)
        A3_UNWRAPN(TIMES[i].len, strftime((char*)TIMES[i].buf, HTTP_TIME_BUF_LENGTH,
                                          HTTP_TIME_FORMAT, gmtime(&tv)));

    return http_response_prep_header(resp, name,
                                     (A3CString) { .ptr = TIMES[i].buf, .len = TIMES[i].len });
}

static bool http_response_prep_header_date(HttpResponse* resp) {
    assert(resp);

    static THREAD_LOCAL uint8_t   DATE_BUF[HTTP_TIME_BUF_LENGTH] = { '\0' };
    static THREAD_LOCAL A3CString DATE                           = A3_CS_NULL;
    static THREAD_LOCAL time_t    LAST_TIME                      = 0;

    time_t current = time(NULL);
    if (current - LAST_TIME > 2 || !DATE.ptr) {
        A3_UNWRAPN(DATE.len, strftime((char*)DATE_BUF, HTTP_TIME_BUF_LENGTH, HTTP_TIME_FORMAT,
                                      gmtime(&current)));
        DATE.ptr  = DATE_BUF;
        LAST_TIME = current;
    }

    return http_response_prep_header(resp, A3_CS("Date"), DATE);
}

// Write the default status line and headers to the send buffer.
static bool http_response_prep_default_headers(HttpResponse* resp, HttpStatus status,
                                               ssize_t content_length, bool close) {
    assert(resp);

    HttpConnection* conn = http_response_connection(resp);

    A3_TRYB(http_response_prep_status_line(resp, status));
    A3_TRYB(http_response_prep_header_date(resp));
    if (close || !http_connection_keep_alive(conn) || content_length < 0) {
        conn->connection_type = HTTP_CONNECTION_TYPE_CLOSE;
        A3_TRYB(http_response_prep_header(resp, A3_CS("Connection"), A3_CS("Close")));
    } else {
        A3_TRYB(http_response_prep_header(resp, A3_CS("Connection"), A3_CS("Keep-Alive")));
    }
    if (content_length >= 0)
        A3_TRYB(
            http_response_prep_header_num(resp, A3_CS("Content-Length"), (size_t)content_length));
    A3_TRYB(http_response_prep_header(resp, A3_CS("Content-Type"),
                                      http_content_type_name(resp->content_type)));

    return true;
}

// Done writing headers.
static bool http_response_prep_headers_done(HttpResponse* resp) {
    assert(resp);

    return a3_buf_write_str(http_response_send_buf(resp), HTTP_NEWLINE);
}

// Write out a response body to the send buffer.
static bool http_response_prep_body(HttpResponse* resp, A3CString body) {
    assert(resp);

    return a3_buf_write_str(http_response_send_buf(resp), body);
}

static A3CString http_response_error_make_body(HttpResponse* resp, HttpStatus status) {
    assert(resp);
    assert(status != HTTP_STATUS_INVALID);

    static uint8_t  body[HTTP_ERROR_BODY_MAX_LENGTH] = { '\0' };
    ssize_t         len                              = 0;
    uint16_t        status_code                      = http_status_code(status);
    HttpConnection* conn                             = http_response_connection(resp);

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
                        status_code, http_version_string(conn->version).ptr, status_code,
                        http_status_reason(status).ptr)) > HTTP_ERROR_BODY_MAX_LENGTH ||
        len < 0)
        return A3_CS_NULL;

    return (A3CString) { .ptr = body, .len = (size_t)len };
}

// Submit a write event for an HTTP error response.
bool http_response_error_submit(HttpResponse* resp, struct io_uring* uring, HttpStatus status,
                                bool close) {
    assert(resp);
    assert(uring);
    assert(status != HTTP_STATUS_INVALID);

    a3_log_fmt(LOG_DEBUG, "HTTP error %d. %s", http_status_code(status),
               close ? "Closing connection." : "");

    // Clear any previously written data.
    a3_buf_reset(http_response_send_buf(resp));

    HttpConnection* conn = http_response_connection(resp);

    conn->state        = HTTP_CONNECTION_RESPONDING;
    resp->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;
    if (conn->version == HTTP_VERSION_INVALID || conn->version == HTTP_VERSION_UNKNOWN)
        conn->version = HTTP_VERSION_11;

    A3CString body = http_response_error_make_body(resp, status);
    A3_TRYB(body.ptr);

    A3_TRYB(http_response_prep_default_headers(resp, status, (ssize_t)body.len, close));
    A3_TRYB(http_response_prep_headers_done(resp));

    if (conn->method != HTTP_METHOD_HEAD)
        A3_TRYB(http_response_prep_body(resp, body));

    A3_TRYB(conn->conn.send_submit(&conn->conn, uring, 0, close ? IOSQE_IO_LINK : 0));
    if (close)
        return http_connection_close_submit(conn, uring);

    return true;
}

bool http_response_file_submit(HttpResponse* resp, struct io_uring* uring) {
    assert(resp);
    assert(uring);

    HttpConnection* conn = http_response_connection(resp);

    if (!conn->target_file) {
        conn->state = HTTP_CONNECTION_OPENING_FILE;
        conn->target_file =
            file_open(EVT(&conn->conn), uring, A3_S_CONST(conn->request.target_path), O_RDONLY);
    }

    if (!conn->target_file)
        return http_response_error_submit(resp, uring, HTTP_STATUS_SERVER_ERROR,
                                          HTTP_RESPONSE_ALLOW);

    if (file_handle_waiting(conn->target_file))
        return true;

    fd target_file = file_handle_fd_unchecked(conn->target_file);
    if (target_file < 0)
        return http_response_error_submit(resp, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);

    struct stat res;
    A3_TRYB(fstat(target_file, &res) == 0);

    bool index = false;

    if (S_ISDIR(res.st_mode)) {
        FileHandle* index_file =
            file_openat(EVT(&conn->conn), uring, conn->target_file, INDEX_FILENAME, O_RDONLY);
        // TODO: Directory listings.
        if (!index_file)
            return http_response_error_submit(resp, uring, HTTP_STATUS_SERVER_ERROR,
                                              HTTP_RESPONSE_ALLOW);

        file_close(conn->target_file, uring);
        conn->target_file = index_file;
        if (file_handle_waiting(conn->target_file))
            return true;

        target_file = file_handle_fd(conn->target_file);
        A3_TRYB(fstat(target_file, &res) == 0);
    }

    if (!S_ISREG(res.st_mode))
        return http_response_error_submit(resp, uring, HTTP_STATUS_NOT_FOUND, HTTP_RESPONSE_ALLOW);

    conn->state = HTTP_CONNECTION_RESPONDING;

    if (index)
        resp->content_type = HTTP_CONTENT_TYPE_TEXT_HTML;
    else
        resp->content_type = http_content_type_from_path(A3_S_CONST(conn->request.target_path));

    A3_TRYB(
        http_response_prep_default_headers(resp, HTTP_STATUS_OK, res.st_size, HTTP_RESPONSE_ALLOW));
    A3_TRYB(http_response_prep_header_timestamp(resp, A3_CS("Last-Modified"), res.st_mtim.tv_sec));
    A3_TRYB(http_response_prep_header_fmt(resp, A3_CS("Etag"), "\"%luX%lX%lX\"", res.st_ino,
                                          res.st_mtime, res.st_size));
    A3_TRYB(http_response_prep_headers_done(resp));

    // TODO: Perhaps instead of just sending here, it would be better to write into the same pipe
    // that is used for splice.
    A3_TRYB(conn->conn.send_submit(
        &conn->conn, uring, (conn->method == HTTP_METHOD_HEAD) ? 0 : MSG_MORE,
        (!http_connection_keep_alive(conn) || conn->method != HTTP_METHOD_HEAD) ? IOSQE_IO_LINK
                                                                                : 0));
    if (conn->method == HTTP_METHOD_HEAD)
        goto done;

    // TODO: This will not work for TLS.
    A3_TRYB(connection_splice_submit(&conn->conn, uring, target_file, (size_t)res.st_size,
                                     !http_connection_keep_alive(conn) ? IOSQE_IO_LINK : 0));

done:
    if (!http_connection_keep_alive(conn))
        return http_connection_close_submit(conn, uring);
    return true;
}
