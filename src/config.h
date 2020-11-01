#pragma once

#include <netinet/in.h>
#include <stdint.h>

#include "log.h"

static const in_port_t DEFAULT_LISTEN_PORT = 8000;
static const uint32_t  LISTEN_BACKLOG      = 1024;

static const uint32_t URING_ENTRIES = 256;

static const enum LogLevel LOG_LEVEL = TRACE;

static const size_t MAX_ALLOCATED_CONNECTIONS = 4096;
static const size_t RECV_BUF_INITIAL_CAPACITY = 2048;
static const size_t RECV_BUF_MAX_CAPACITY     = 10240;
static const size_t SEND_BUF_INITIAL_CAPACITY = 2048;
static const size_t SEND_BUF_MAX_CAPACITY     = 4096;

#define HTTP_ERROR_BODY_MAX_LENGTH 512
static const size_t HTTP_REQUEST_LINE_MAX_LENGTH = 2048;
