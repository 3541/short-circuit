#include "http/connection.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <a3/buffer.h>
#include <a3/log.h>
#include <a3/pool.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "http/types.h"
#include "uri.h"

static Pool* HTTP_CONNECTION_POOL = NULL;

void http_connection_pool_init() {
    HTTP_CONNECTION_POOL =
        pool_new(sizeof(HttpConnection), CONNECTION_MAX_ALLOCATED);
}

HttpConnection* http_connection_new() {
    HttpConnection* ret = pool_alloc_block(HTTP_CONNECTION_POOL);

    if (ret && !http_connection_init(ret))
        http_connection_free(ret, NULL);

    return ret;
}

void http_connection_free(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    // If the socket hasn't been closed, arrange it. The close handle event will
    // call free when it's done.
    if (this->conn.socket != -1) {
        event_cancel_all(EVT(&this->conn), uring, IOSQE_IO_HARDLINK);
        // If the submission was successful, we're done for now.
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

    http_connection_reset(this, uring);

    if (buf_initialized(&this->conn.recv_buf))
        buf_destroy(&this->conn.recv_buf);
    if (buf_initialized(&this->conn.send_buf))
        buf_destroy(&this->conn.send_buf);

    pool_free_block(HTTP_CONNECTION_POOL, this);
}

void http_connection_pool_free() { pool_free(HTTP_CONNECTION_POOL); }

bool http_connection_init(HttpConnection* this) {
    assert(this);

    TRYB(connection_init(&this->conn));

    this->state                       = CONNECTION_INIT;
    this->version                     = HTTP_VERSION_11;
    this->keep_alive                  = true;
    this->content_length              = -1;
    this->transfer_encodings          = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->target_file                 = -1;
    this->response_transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;

    return true;
}

bool http_connection_reset(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    if (this->host.ptr)
        string_free(&this->host);

    if (this->target_path.ptr)
        string_free(&this->target_path);

    if (uri_is_initialized(&this->target))
        uri_free(&this->target);

    if (this->target_file >= 0) {
        event_close_submit(NULL, uring, this->target_file, 0,
                           EVENT_FALLBACK_ALLOW);
        this->target_file = -1;
    }

    return connection_reset(&this->conn, uring);
}
