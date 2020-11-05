#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#include "forward.h"
#include "http_types.h"
#include "ptr.h"
#include "uri.h"

#define HTTP_NEWLINE CS("\r\n")

typedef enum HttpRequestState {
    REQUEST_INIT,
    REQUEST_PARSED_FIRST_LINE,
    REQUEST_PARSED_HEADERS,
    REQUEST_RESPONDING,
    REQUEST_CLOSING,
} HttpRequestState;

typedef struct HttpRequest {
    HttpRequestState state;

    HttpVersion version;
    HttpMethod  method;
    Uri         target;
    String      target_path;

    bool                 keep_alive;
    CString              host;
    HttpTransferEncoding transfer_encodings;
    ssize_t              content_length;

    HttpContentType      response_content_type;
    HttpTransferEncoding response_transfer_encodings;
} HttpRequest;

void http_request_reset(HttpRequest*);

HttpRequestResult http_request_handle(Connection*, struct io_uring*);

bool http_response_handle(Connection*, struct io_uring*);

#define HTTP_RESPONSE_CLOSE true
#define HTTP_RESPONSE_ALLOW false
bool http_response_error_submit(Connection*, struct io_uring*, HttpStatus,
                                bool close);
