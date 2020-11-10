#include "http_connection.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "event.h"
#include "forward.h"
#include "http_types.h"
#include "log.h"
#include "ptr.h"
#include "uri.h"

static HttpConnection* http_conn_freelist  = NULL;
static size_t          http_conn_allocated = 0;

HttpConnection* http_connection_new() {
    HttpConnection* ret = NULL;
    if (http_conn_freelist) {
        ret                = http_conn_freelist;
        http_conn_freelist = (HttpConnection*)ret->conn.next;
    } else if (http_conn_allocated < CONNECTION_MAX_ALLOCATED) {
        ret = calloc(1, sizeof(HttpConnection));
        http_conn_allocated++;
    } else {
        ERR("Too many connections allocated.");
    }

    if (ret)
        http_connection_init(ret);

    return ret;
}

void http_connection_free(HttpConnection* this, struct io_uring* uring) {
    assert(this);

    // If the socket hasn't been closed, arrange it. The close handle event will
    // call free when it's done.
    if (this->conn.last_event.type != INVALID_EVENT &&
        this->conn.last_event.type != CLOSE) {
        // If the submission was successful, we're done.
        if (connection_close_submit(&this->conn, uring))
            return;

        // Make a last-ditch attempt to close, but do not block. Theoretically
        // this could cause a leak of sockets, but if both the close request and
        // the actual close here fail, there are probably larger issues at play.
        int flags = fcntl(this->conn.socket, F_GETFL);
        if (fcntl(this->conn.socket, F_SETFL, flags | O_NONBLOCK) != 0 ||
            close(this->conn.socket) != 0)
            log_error(errno, "Failed to close socket.");
    }

    http_connection_reset(this);

    buf_free(&this->conn.recv_buf);
    if (buf_initialized(&this->conn.send_buf))
        buf_free(&this->conn.send_buf);

    this->conn.next    = (Connection*)http_conn_freelist;
    http_conn_freelist = this;
}

void http_connection_freelist_clear() {
    for (HttpConnection* conn = http_conn_freelist; conn;) {
        HttpConnection* next = (HttpConnection*)conn->conn.next;
        free(conn);
        conn = next;
    }
}

void http_connection_init(HttpConnection* this) {
    assert(this);

    this->state                       = CONNECTION_INIT;
    this->version                     = HTTP_VERSION_11;
    this->keep_alive                  = true;
    this->content_length              = -1;
    this->transfer_encodings          = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->target_file                 = -1;
    this->response_transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;
}

void http_connection_reset(HttpConnection* this) {
    assert(this);

    if (this->host.ptr)
        string_free(&this->host);

    if (this->target_path.ptr)
        string_free(&this->target_path);

    if (uri_is_initialized(&this->target))
        uri_free(&this->target);

    if (this->target_file >= 0)
        close(this->target_file);

    connection_reset(&this->conn);
}
