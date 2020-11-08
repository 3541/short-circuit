#pragma once

#include <stdbool.h>

#include "forward.h"
#include "http_types.h"

bool http_response_handle(Connection*, struct io_uring*);

#define HTTP_RESPONSE_CLOSE true
#define HTTP_RESPONSE_ALLOW false
bool http_response_error_submit(Connection*, struct io_uring*, HttpStatus,
                                bool close);

bool http_response_file_submit(Connection*, struct io_uring*);
