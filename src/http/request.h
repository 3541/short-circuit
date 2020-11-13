#pragma once

#include "forward.h"
#include "http/types.h"

void http_request_reset(HttpConnection*);

HttpRequestResult http_request_handle(HttpConnection*, struct io_uring*);
