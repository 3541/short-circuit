#pragma once

#include <netinet/in.h>
#include <stdint.h>

#include "log.h"

#define MIN_KERNEL_VERSION_MAJOR 5
#define MIN_KERNEL_VERSION_MINOR 6

#define DEFAULT_LISTEN_PORT 8000
#define LISTEN_BACKLOG      1024
#define DEFAULT_WEB_ROOT    "/var/www/localhost/htdocs"
#define INDEX_FILENAME      "index.html"

#define URING_ENTRIES              512
#define URING_SUBMISSION_THRESHOLD 500

#ifdef DEBUG_BUILD
#define LOG_LEVEL DEBUG
#else
#define LOG_LEVEL WARN
#endif

#define CONNECTION_MAX_ALLOCATED 4096

#define RECV_BUF_INITIAL_CAPACITY 2048
#define RECV_BUF_MAX_CAPACITY     10240
#define SEND_BUF_INITIAL_CAPACITY 2048
#define SEND_BUF_MAX_CAPACITY     10240

#define HTTP_ERROR_BODY_MAX_LENGTH      512
#define HTTP_REQUEST_LINE_MAX_LENGTH    2048
#define HTTP_REQUEST_HEADER_MAX_LENGTH  2048
#define HTTP_REQUEST_HOST_MAX_LENGTH    512
#define HTTP_REQUEST_URI_MAX_LENGTH     512
#define HTTP_REQUEST_CONTENT_MAX_LENGTH 10240
