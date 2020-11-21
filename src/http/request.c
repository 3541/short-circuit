#include "http/request.h"

#include <assert.h>
#include <fcntl.h>

#include <a3/log.h>
#include <a3/util.h>

#include "http/connection.h"
#include "http/parse.h"
#include "http/response.h"
#include "http/types.h"
#include "ptr.h"

// TODO: Perhaps handle things other than static files.
static HttpRequestStateResult
http_request_get_head_handle(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    // TODO: GET things other than static files.
    if ((this->target_file =
             open((const char*)this->target_path.ptr, O_RDONLY)) < 0)
        RET_MAP(http_response_error_submit(
                    this, uring, HTTP_STATUS_SERVER_ERROR, HTTP_RESPONSE_CLOSE),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);

    log_fmt(TRACE, "Sending file %s.", this->target_path.ptr);
    RET_MAP(http_response_file_submit(this, uring), HTTP_REQUEST_STATE_DONE,
            HTTP_REQUEST_STATE_ERROR);
}

// Do whatever is appropriate for the parsed method.
static HttpRequestStateResult
http_request_method_handle(HttpConnection* this, struct io_uring* uring) {
    assert(this);
    assert(uring);

    switch (this->method) {
    case HTTP_METHOD_HEAD:
    case HTTP_METHOD_GET:
        return http_request_get_head_handle(this, uring);
    case HTTP_METHOD_BREW:
        this->version = HTCPCP_VERSION_10;
        RET_MAP(http_response_error_submit(this, uring, HTTP_STATUS_IM_A_TEAPOT,
                                           HTTP_RESPONSE_ALLOW),
                HTTP_REQUEST_STATE_BAIL, HTTP_REQUEST_STATE_ERROR);
    case HTTP_METHOD_INVALID:
    case HTTP_METHOD_UNKNOWN:
        UNREACHABLE();
    }

    UNREACHABLE();
}

// Try to parse as much of the HTTP request as possible.
HttpRequestResult http_request_handle(HttpConnection* this,
                                      struct io_uring* uring) {
    assert(this);
    assert(uring);

    HttpRequestStateResult rc = HTTP_REQUEST_STATE_ERROR;

    // Go through as many states as possible with the data currently loaded.
    switch (this->state) {
    case CONNECTION_INIT:
        http_connection_init(this);
        if ((rc = http_request_first_line_parse(this, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case CONNECTION_PARSED_FIRST_LINE:
        if ((rc = http_request_headers_parse(this, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case CONNECTION_PARSED_HEADERS:
        if ((rc = http_request_method_handle(this, uring)) !=
            HTTP_REQUEST_STATE_DONE)
            return (HttpRequestResult)rc;
        // fallthrough
    case CONNECTION_RESPONDING:
    case CONNECTION_CLOSING:
        return HTTP_REQUEST_COMPLETE;
    }

    log_fmt(TRACE, "State: %d", this->state);
    PANIC("TODO: Handle whatever request did this.");
}
