#pragma once

#include <stdint.h>

#include "forward.h"
#include "http_types.h"
#include "ptr.h"

CString http_version_string(enum HttpVersion);
CString http_status_reason(enum HttpStatus);
CString http_content_type_name(enum HttpContentType);

int8_t http_request_first_line_parse(struct Connection*, struct io_uring*);
int8_t http_request_headers_parse(struct Connection*, struct io_uring*);
