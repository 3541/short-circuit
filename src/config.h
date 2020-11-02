#pragma once

#include <netinet/in.h>
#include <stdint.h>

#include "log.h"

static const uint8_t MIN_KERNEL_VERSION_MAJOR = 5;
static const uint8_t MIN_KERNEL_VERSION_MINOR = 6;

static const in_port_t DEFAULT_LISTEN_PORT = 8000;
static const uint32_t  LISTEN_BACKLOG      = 1024;

static const uint32_t URING_ENTRIES = 256;

static const enum LogLevel LOG_LEVEL = TRACE;

static const size_t CONNECTION_MAX_ALLOCATED  = 4096;
static const size_t RECV_BUF_INITIAL_CAPACITY = 2048;
static const size_t RECV_BUF_MAX_CAPACITY     = 10240;
static const size_t SEND_BUF_INITIAL_CAPACITY = 2048;
static const size_t SEND_BUF_MAX_CAPACITY     = 4096;

#define HTTP_ERROR_BODY_MAX_LENGTH 512
static const size_t HTTP_REQUEST_LINE_MAX_LENGTH   = 2048;
static const size_t HTTP_REQUEST_HEADER_MAX_LENGTH = 2048;
static const size_t HTTP_REQUEST_HOST_MAX_LENGTH   = 512;
static const size_t HTTP_REQUEST_URI_MAX_LENGTH    = 512;
static const size_t HTTP_REQUEST_CONTENT_MAX_LENGTH = 10240;
