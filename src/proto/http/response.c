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

#include <sc/coroutine.h>
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

    sc_http_response_reset(resp);
    sc_co_defer(sc_http_response_destroy, resp);
}

void sc_http_response_reset(ScHttpResponse* resp) {
    assert(resp);

    resp->content_type = A3_CS_NULL;

    if (sc_http_headers_count(&resp->headers) > 0)
        sc_http_headers_destroy(&resp->headers);
    sc_http_headers_init(&resp->headers);
}

void sc_http_response_destroy(void* data) {
    assert(data);

    ScHttpResponse* resp = data;

    sc_http_headers_destroy(&resp->headers);
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

    return sc_http_header_set(&resp->headers, A3_CS("Date"), DATE);
}

static bool sc_http_headers_default_prep(ScHttpResponse* resp) {
    assert(resp);

    ScHttpConnection* conn = sc_http_response_connection(resp);

    A3_TRYB(sc_http_response_header_date_prep(resp));
    A3_TRYB(sc_http_header_set(&resp->headers, A3_CS("Connection"),
                               sc_http_connection_keep_alive(conn) ? A3_CS("Keep-Alive")
                                                                   : A3_CS("Close")));
    A3_TRYB(sc_http_header_set_num(&resp->headers, A3_CS("Content-Length"),
                                   a3_buf_len(sc_http_response_send_buf(resp))));
    A3_TRYB(sc_http_header_set(&resp->headers, A3_CS("Content-Type"), resp->content_type));

    return true;
}

void sc_http_response_send(ScHttpResponse* resp, ScHttpStatus status) {
    assert(resp);
    assert(status != SC_HTTP_STATUS_INVALID);

    ScHttpConnection* conn = sc_http_response_connection(resp);
    A3Buffer*         buf  = sc_http_response_send_buf(resp);

    if (!sc_http_headers_default_prep(resp)) {
        sc_connection_close(conn->conn);
        A3_WARN("Failed preparing default headers.");
        return;
    }

    struct iovec  iov_local[256];
    struct iovec* iov = iov_local;

    size_t iov_count = 4 * sc_http_headers_count(&resp->headers) + 3;
    if (conn->request.method == SC_HTTP_METHOD_HEAD)
        iov_count--;
    if (iov_count > sizeof(iov_local) / sizeof(iov_local[0]))
        A3_UNWRAPN(iov, calloc(iov_count, sizeof(*iov)));

    uint8_t  status_line[PATH_MAX + 128] = { '\0' };
    A3Buffer status_buf = { .data    = a3_string_new(status_line, sizeof(status_line)),
                            .max_cap = sizeof(status_line),
                            .head    = 0,
                            .tail    = 0 };
    a3_buf_write_fmt(&status_buf, A3_S_F " %d " A3_S_F A3_S_F,
                     A3_S_FORMAT(sc_http_version_string(conn->version)), status,
                     A3_S_FORMAT(sc_http_status_reason(status)), A3_S_FORMAT(SC_HTTP_EOL));
    iov[0] = (struct iovec) { .iov_base = status_line, a3_buf_len(&status_buf) };

    size_t i = 0;
    SC_HTTP_HEADERS_FOR_EACH(&resp->headers, name, value) {
        assert(i < iov_count);

        iov[++i] = (struct iovec) { .iov_base = (void*)name->ptr, .iov_len = name->len };
        iov[++i] = (struct iovec) { .iov_base = ": ", .iov_len = 2 };
        iov[++i] = (struct iovec) { .iov_base = (void*)value->ptr, .iov_len = value->len };
        iov[++i] =
            (struct iovec) { .iov_base = (void*)SC_HTTP_EOL.ptr, .iov_len = SC_HTTP_EOL.len };
    }
    iov[++i] = (struct iovec) { .iov_base = (void*)SC_HTTP_EOL.ptr, .iov_len = SC_HTTP_EOL.len };
    if (conn->request.method != SC_HTTP_METHOD_HEAD)
        iov[++i] = (struct iovec) { .iov_base = (void*)a3_buf_read_ptr(buf).ptr,
                                    .iov_len  = a3_buf_len(buf) };

    if (SC_IO_IS_ERR(sc_io_writev(conn->conn->socket, iov, (unsigned)iov_count))) {
        A3_WARN("Failed to send response: writev error.");
        sc_connection_close(conn->conn);
    }

    a3_buf_reset(buf);

    if (iov != iov_local)
        free(iov);
}

static bool sc_http_response_error_body_prep(ScHttpResponse* resp, ScHttpStatus status) {
    assert(resp);
    assert(status >= 400);

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

    sc_http_response_send(resp, status);
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
        SC_IO_RESULT(ScFd)
        maybe_file = sc_io_open_under(file, SC_INDEX_FILENAME, O_RDONLY);

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

    // TODO: Last-Modified.
    if (!sc_http_header_set_fmt(&resp->headers, A3_CS("Etag"), "\"%lluX%lX%lX\"", statbuf.st_ino,
                                statbuf.st_mtim.tv_sec, statbuf.st_size)) {
        A3_WARN("Failed to write Etag.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
        return;
    }
    if (!sc_http_header_set_time(&resp->headers, A3_CS("Last-Modified"), statbuf.st_mtim.tv_sec)) {
        A3_WARN("Failed to write Last-Modified.");
        sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
        return;
    }

    A3Buffer* buf = sc_http_response_send_buf(resp);
    if (!a3_buf_ensure_cap(buf, (size_t)statbuf.st_size))
        A3_PANIC("TODO: Handle files larger than the send buffer.");

    if (conn->request.method != SC_HTTP_METHOD_HEAD) {
        SC_IO_RESULT(size_t)
        maybe_size = sc_io_read(file, a3_buf_write_ptr(buf), (size_t)statbuf.st_size, 0);
        if (SC_IO_IS_ERR(maybe_size)) {
            A3_WARN_F("Failed to read file \"" A3_S_F "\".", A3_S_FORMAT(path));
            sc_http_response_error_send(resp, SC_HTTP_STATUS_SERVER_ERROR, SC_HTTP_CLOSE);
            return;
        }

        a3_buf_wrote(buf, maybe_size.ok);
    }

    SC_IO_UNWRAP(sc_io_close(file));

    sc_http_response_send(resp, SC_HTTP_STATUS_OK);
}
