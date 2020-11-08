#include "http_request.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "buffer.h"
#include "config.h"
#include "connection.h"
#include "http_parse.h"
#include "http_response.h"
#include "http_types.h"
#include "ptr.h"
#include "ptr_util.h"
#include "uri.h"
#include "util.h"


static void http_request_init(HttpRequest* this) {
    assert(this);

    this->state                       = REQUEST_INIT;
    this->version                     = HTTP_VERSION_11;
    this->keep_alive                  = true;
    this->content_length              = -1;
    this->transfer_encodings          = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->target_file                 = -1;
    this->response_transfer_encodings = HTTP_TRANSFER_ENCODING_IDENTITY;
    this->response_content_type       = HTTP_CONTENT_TYPE_TEXT_HTML;
}

void http_request_reset(HttpRequest* this) {
    assert(this);

    if (this->host.ptr)
        string_free(CS_MUT(this->host));

    if (this->target_path.ptr)
        string_free(this->target_path);

    if (uri_is_initialized(&this->target))
        uri_free(&this->target);

    if (this->target_file >= 0)
        close(this->target_file);

    memset(this, 0, sizeof(HttpRequest));
}

// TODO: Perhaps handle things other than static files.
static HttpRequestStateResult
http_request_get_head_handle(Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;

    // TODO: GET things other than static files.
    if ((this->target_file = open(this->target_path.ptr, O_RDONLY)) < 0)
        RET_MAP(http_response_error_submit(
                    conn, uring, HTTP_STATUS_SERVER_ERROR, HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    log_fmt(TRACE, "Sending file %s.", this->target_path.ptr);
    RET_MAP(http_response_file_submit(conn, uring), HTTP_REQUEST_STATE_DONE,
            HTTP_REQUEST_STATE_ERROR);
}

// Do whatever is appropriate for the parsed method.
static HttpRequestStateResult
http_request_method_handle(Connection* conn, struct io_uring* uring) {
    assert(conn);
    assert(uring);

    struct HttpRequest* this = &conn->request;

    switch (this->method) {
    case HTTP_METHOD_HEAD:
    case HTTP_METHOD_GET:
        return http_request_get_head_handle(conn, uring);
    case HTTP_METHOD_BREW:
        this->version = HTCPCP_VERSION_10;
        RET_MAP(http_response_error_submit(conn, uring, HTTP_STATUS_IM_A_TEAPOT,
                                           HTTP_RESPONSE_ALLOW),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case HTTP_METHOD_INVALID:
    case HTTP_METHOD_UNKNOWN:
        UNREACHABLE();
    }

    UNREACHABLE();
}

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
        if ((rc = http_request_first_line_parse(conn, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case REQUEST_PARSED_FIRST_LINE:
        if ((rc = http_request_headers_parse(conn, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case REQUEST_PARSED_HEADERS:
        if ((rc = http_request_method_handle(conn, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case REQUEST_RESPONDING:
    case REQUEST_CLOSING:
        return HTTP_REQUEST_COMPLETE;
    }

    log_fmt(TRACE, "State: %d", this->state);
    PANIC("TODO: Handle whatever request did this.");
}
