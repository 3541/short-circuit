#pragma once

#include <stdint.h>

#include "forward.h"

#define HTTP_METHOD_ENUM _METHOD(GET)

enum HttpMethod {
#define _METHOD(M) M,
    HTTP_METHOD_ENUM
#undef _METHOD
};

struct HttpRequest {
    enum HttpMethod method;
};

int8_t http_request_parse(struct Connection*, struct io_uring*);
