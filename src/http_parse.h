#pragma once

#include <stdint.h>

#include "forward.h"
#include "http_types.h"

const char* http_version_string(enum HttpVersion);
const char* http_status_reason(enum HttpStatus);
const char* http_content_type_name(enum HttpContentType);

int8_t http_request_first_line_parse(struct Connection*, struct io_uring*);
int8_t http_request_headers_parse(struct Connection*, struct io_uring*);
