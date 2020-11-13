#pragma once

#include "forward.h"
#include "http/types.h"
#include "ptr.h"

CString         http_version_string(HttpVersion);
CString         http_status_reason(HttpStatus);
CString         http_content_type_name(HttpContentType);
HttpContentType http_content_type_from_path(CString);

HttpRequestStateResult http_request_first_line_parse(HttpConnection*,
                                                     struct io_uring*);
HttpRequestStateResult http_request_headers_parse(HttpConnection*,
                                                  struct io_uring*);
