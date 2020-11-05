#pragma once

#include <stdint.h>

#include "forward.h"
#include "http_types.h"
#include "ptr.h"

CString http_version_string(enum HttpVersion);
CString http_status_reason(enum HttpStatus);
CString http_content_type_name(enum HttpContentType);

enum HttpRequestStateResult http_request_first_line_parse(struct Connection*, struct io_uring*);
enum HttpRequestStateResult http_request_headers_parse(struct Connection*, struct io_uring*);
