#pragma once

#include "forward.h"
#include "http/types.h"

HttpRequestStateResult http_request_first_line_parse(HttpConnection*,
                                                     struct io_uring*);
HttpRequestStateResult http_request_headers_parse(HttpConnection*,
                                                  struct io_uring*);
