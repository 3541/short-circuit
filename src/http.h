#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "forward.h"
#include "http_types.h"
#include "ptr.h"
#include "uri.h"

#define HTTP_NEWLINE CS("\r\n")

enum HttpRequestState {
    REQUEST_INIT,
    REQUEST_PARSED_FIRST_LINE,
    REQUEST_PARSED_HEADERS,
    REQUEST_RESPONDING,
    REQUEST_CLOSING,
};

struct HttpRequest {
    enum HttpRequestState state;

    enum HttpVersion version;
    enum HttpMethod  method;
    struct Uri       target;

    bool                 keep_alive;
    CString              host;
    HttpTransferEncoding transfer_encodings;
    ssize_t              content_length;

    enum HttpContentType response_content_type;
    HttpTransferEncoding response_transfer_encodings;
};

void   http_request_reset(struct HttpRequest*);

enum HttpRequestResult http_request_handle(struct Connection*, struct io_uring*);

bool http_response_handle(struct Connection*, struct io_uring*);

#define HTTP_RESPONSE_CLOSE true
#define HTTP_RESPONSE_ALLOW false
bool http_response_error_submit(struct Connection*, struct io_uring*,
                                enum HttpStatus, bool close);
