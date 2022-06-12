/*
 * SHORT CIRCUIT: HTTP RESPONSE -- HTTP response submission.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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

#include "response.h"

#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/uio.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include <sc/http.h>
#include <sc/mime.h>

#include "config.h"
#include "connection.h"
#include "headers.h"

static ScHttpConnection* sc_http_response_connection(ScHttpResponse* resp) {
    assert(resp);

    return A3_CONTAINER_OF(resp, ScHttpConnection, response);
}

static A3Buffer* sc_http_response_send_buf(ScHttpResponse* resp) {
    assert(resp);

    return &sc_http_response_connection(resp)->conn->send_buf;
}

void sc_http_response_init(ScHttpResponse* resp) {
    assert(resp);

    *resp = (ScHttpResponse) {
        .content_length   = SC_HTTP_CONTENT_LENGTH_UNSPECIFIED,
        .content_type     = A3_CS_NULL,
        .target           = SC_HTTP_RESPONSE_NONE,
        .target_data.file = -1,
    };
    a3_buf_init(&resp->pre_buf, SC_HTTP_HEADER_BUF_INIT_CAP, SC_HTTP_HEADER_BUF_MAX_CAP);
}

static void sc_http_response_target_close(ScHttpResponse* resp) { assert(resp); }

void sc_http_response_reset(ScHttpResponse* resp) {
    assert(resp);

    a3_buf_reset(&resp->pre_buf);

    resp->content_length = SC_HTTP_CONTENT_LENGTH_UNSPECIFIED;
    resp->content_type   = A3_CS_NULL;

    switch (resp->target) {
    case SC_HTTP_RESPONSE_FILE:
        SC_IO_UNWRAP(sc_io_close(resp->target_data.file));
        break;
    case SC_HTTP_RESPONSE_STR:
    case SC_HTTP_RESPONSE_NONE:
        break;
    }

    resp->target = SC_HTTP_RESPONSE_NONE;
}

void sc_http_response_destroy(void* data) {
    assert(data);

    ScHttpResponse* resp = data;

    a3_buf_destroy(&resp->pre_buf);
    sc_http_response_target_close(resp);
}

static bool sc_http_response_header_date_prep(ScHttpResponse* resp) {
    assert(resp);

    static A3_THREAD_LOCAL uint8_t   DATE_BUF[SC_HTTP_TIME_BUF_SIZE] = { '\0' };
    static A3_THREAD_LOCAL A3CString DATE                            = A3_CS_NULL;
    static A3_THREAD_LOCAL time_t    LAST_TIME                       = 0;

    time_t current = time(NULL);
    if (current - LAST_TIME > 2 || !DATE.ptr) {
        struct tm tv;
        A3_UNWRAPN(DATE.len, strftime((char*)DATE_BUF, SC_HTTP_TIME_BUF_SIZE, SC_HTTP_TIME_FORMAT,
                                      gmtime_r(&current, &tv)));
        DATE.ptr  = DATE_BUF;
        LAST_TIME = current;
    }

    return a3_buf_write_fmt(&resp->pre_buf, "Date: " A3_S_F "\r\n", A3_S_FORMAT(DATE));
}

static bool sc_http_response_status_line_prep(ScHttpResponse* resp, ScHttpStatus status) {
    assert(resp);
    assert(status != SC_HTTP_STATUS_INVALID);

    ScHttpConnection* conn = sc_http_response_connection(resp);

    return a3_buf_write_fmt(&resp->pre_buf, A3_S_F " %d " A3_S_F "\r\n",
                            A3_S_FORMAT(sc_http_version_string(conn->version)), status,
                            A3_S_FORMAT(sc_http_status_reason(status)));
}

static bool sc_http_response_headers_default_prep(ScHttpResponse* resp) {
    assert(resp);

    ScHttpConnection* conn = sc_http_response_connection(resp);

    A3_TRYB(sc_http_response_header_date_prep(resp));
    A3_TRYB(a3_buf_write_fmt(&resp->pre_buf, "Connection: %s\r\n",
                             sc_http_connection_keep_alive(conn) ? "Keep-Alive" : "Close"));
    if (resp->content_length != SC_HTTP_CONTENT_LENGTH_UNSPECIFIED)
        A3_TRYB(a3_buf_write_fmt(&resp->pre_buf, "Content-Length: %zu\r\n",
                                 (size_t)resp->content_length));
    if (resp->content_type.ptr)
        A3_TRYB(a3_buf_write_fmt(&resp->pre_buf, "Content-Type: " A3_S_F "\r\n",
                                 A3_S_FORMAT(resp->content_type)));

    return true;
}

static bool sc_http_response_start_prep(ScHttpResponse* resp, ScHttpStatus status) {
    assert(resp);
    assert(status != SC_HTTP_STATUS_INVALID);

    if (!sc_http_response_status_line_prep(resp, status) ||
        !sc_http_response_headers_default_prep(resp)) {
        A3_WARN("Failed preparing pre-body section.");
        sc_connection_close(sc_http_response_connection(resp)->conn);
        return false;
    }

    return true;
}

void sc_http_response_send(ScHttpResponse* resp) {
    assert(resp);

    ScHttpConnection* conn = sc_http_response_connection(resp);

    A3_TRACE_F("Sending response:\n" A3_S_F, A3_S_FORMAT(a3_buf_read_ptr(&resp->pre_buf)));

    struct iovec iov[3] = {
        { .iov_base = (void*)a3_buf_read_ptr(&resp->pre_buf).ptr,
          .iov_len  = a3_buf_read_ptr(&resp->pre_buf).len },
        { .iov_base = "\r\n", .iov_len = 2 },
    };
    unsigned int iov_count = 2;

    switch (resp->target) {
    case SC_HTTP_RESPONSE_FILE: {
        ScFd      file = resp->target_data.file;
        size_t    size = (size_t)resp->content_length;
        A3Buffer* buf  = sc_http_response_send_buf(resp);

        if (conn->request.method == SC_HTTP_METHOD_HEAD)
            break;

        SC_IO_RESULT(size_t) maybe_size = sc_io_read(file, a3_buf_write_ptr(buf), size, 0);
        if (SC_IO_IS_ERR(maybe_size)) {
            A3_WARN("Failed to read requested file.");
            sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
            return;
        }

        a3_buf_wrote(buf, maybe_size.ok);

        iov[2].iov_base = (void*)a3_buf_read_ptr(buf).ptr;
        iov[2].iov_len  = a3_buf_read_ptr(buf).len;
        iov_count       = 3;

        break;
    }
    case SC_HTTP_RESPONSE_STR:
        iov[2].iov_base = (void*)a3_string_cptr(resp->target_data.str);
        iov[2].iov_len  = a3_string_len(resp->target_data.str);
        iov_count       = 3;

        break;
    default:
        break;
    }

    if (SC_IO_IS_ERR(sc_io_writev(conn->conn->socket, iov, iov_count))) {
        A3_WARN("Failed to send response: writev error.");
        sc_connection_close(conn->conn);
    }
}

static bool sc_http_response_error_body_prep(ScHttpResponse* resp, ScHttpStatus status) {
    assert(resp);
    assert(status >= 400);

    A3Buffer* buf = sc_http_response_send_buf(resp);

    size_t init_len = a3_buf_len(buf);

    A3_TRYB(a3_buf_write_fmt(
        sc_http_response_send_buf(resp),
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<title>Error: %d</title>\n"
        "</head>\n"
        "<body>\n"
        "<h1>" A3_S_F " Error %d</h1>\n"
        "<p>" A3_S_F ".</p>\n"
        "</body>\n"
        "</html>\n",
        status, A3_S_FORMAT(sc_http_version_string(sc_http_response_connection(resp)->version)),
        status, A3_S_FORMAT(sc_http_status_reason(status))));

    size_t wrote = (a3_buf_len(buf) - init_len);
    assert(wrote <= SSIZE_MAX);
    resp->content_length  = (ssize_t)wrote;
    resp->target          = SC_HTTP_RESPONSE_STR;
    resp->target_data.str = a3_buf_read_ptr(buf);

    return true;
}

void sc_http_response_error_send(ScHttpResponse* resp, ScHttpStatus status, bool close) {
    assert(resp);
    assert(status >= 400);

    A3_TRACE_F("HTTP error %d. " A3_S_F, status, sc_http_status_reason(status));
    ScHttpConnection* conn = sc_http_response_connection(resp);

    if (close)
        conn->connection_type = SC_HTTP_CONNECTION_TYPE_CLOSE;

    // Clear any data already written to the response buffer.
    a3_buf_reset(sc_http_response_send_buf(resp));
    // Clear any further data from the request which provoked the error.
    a3_buf_reset(&conn->conn->recv_buf);

    resp->content_type = SC_MIME_TYPE_TEXT_HTML;
    if (conn->version == SC_HTTP_VERSION_UNKNOWN || conn->version == SC_HTTP_VERSION_INVALID)
        conn->version = SC_HTTP_VERSION_11;

    if (!sc_http_response_error_body_prep(resp, status)) {
        A3_WARN("Failed to write HTTP error response body.");
        sc_connection_close(conn->conn);
        return;
    }

    if (!sc_http_response_start_prep(resp, status))
        return;

    sc_http_response_send(resp);

    if (close)
        sc_connection_close(conn->conn);
}

void sc_http_response_file_send(ScHttpResponse* resp, ScFd file) {
    assert(resp);
    assert(file >= 0);

    A3_TRACE("Sending file.");

    ScHttpConnection* conn = sc_http_response_connection(resp);
    A3CString         path = conn->request.target.path;

    struct stat statbuf;
    if (SC_IO_IS_ERR(sc_io_stat(file, &statbuf))) {
        A3_TRACE_F("Failed to stat file \"" A3_S_F "\".", A3_S_FORMAT(path));
        sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_FOUND, SC_HTTP_KEEP);
        return;
    }

    bool index = false;
    if (S_ISDIR(statbuf.st_mode)) {
        SC_IO_RESULT(ScFd) maybe_file = sc_io_open_under(file, SC_INDEX_FILENAME, O_RDONLY);

        // TODO: Directory listings.
        if (SC_IO_IS_ERR(maybe_file)) {
            A3_TRACE("Requested directory and no index is present.");
            sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_FOUND, SC_HTTP_KEEP);
            return;
        }
        if (SC_IO_IS_ERR(sc_io_close(file))) {
            A3_WARN("Failed to close directory fd.");
            sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
            return;
        }
        file = maybe_file.ok;

        if (SC_IO_IS_ERR(sc_io_stat(file, &statbuf))) {
            A3_WARN("Failed to stat index file.");
            sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
            return;
        }

        index = true;
    }

    if (!S_ISREG(statbuf.st_mode)) {
        A3_TRACE("Requested non-regular file.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_NOT_FOUND, SC_HTTP_KEEP);
        return;
    }

    if (index)
        resp->content_type = SC_MIME_TYPE_TEXT_HTML;
    else
        resp->content_type = sc_mime_from_path(path);

    resp->content_length = statbuf.st_size;

    if (!sc_http_response_start_prep(resp, SC_HTTP_STATUS_OK))
        return;

    if (!a3_buf_write_fmt(&resp->pre_buf, "ETag: \"%lluX%lX%lX\"\r\n", statbuf.st_ino,
                          statbuf.st_mtim.tv_sec, statbuf.st_size)) {
        A3_WARN("Failed to write Etag.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
        return;
    }

    uint8_t   time_buf[SC_HTTP_TIME_BUF_SIZE] = { '\0' };
    struct tm tv;

    size_t len = strftime((char*)time_buf, SC_HTTP_TIME_BUF_SIZE, SC_HTTP_TIME_FORMAT,
                          gmtime_r(&statbuf.st_mtim.tv_sec, &tv));
    if (!len) {
        A3_WARN("Failed to format Last-Modified header.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
        return;
    }
    if (!a3_buf_write_fmt(&resp->pre_buf, "Last-Modified: %s\r\n", time_buf)) {
        A3_WARN("Failed to write Last-Modified.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
        return;
    }

    resp->target           = SC_HTTP_RESPONSE_FILE;
    resp->target_data.file = file;

    A3Buffer* buf = sc_http_response_send_buf(resp);
    if (!a3_buf_ensure_cap(buf, (size_t)statbuf.st_size))
        A3_PANIC("TODO: Handle files larger than the send buffer.");

    sc_http_response_send(resp);
}
