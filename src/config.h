/*
 * SHORT CIRCUIT: CONFIG -- Configurable settings.
 *
 * Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <netinet/in.h>
#include <stdint.h>

// This is the minimum kernel version which supports IORING_REGISTER_PROBE.
#define MIN_KERNEL_VERSION_MAJOR 5
#define MIN_KERNEL_VERSION_MINOR 6

#define DEFAULT_LISTEN_PORT 8000
#define LISTEN_BACKLOG      1024
#define DEFAULT_WEB_ROOT    "."
#define INDEX_FILENAME      A3_CS("index.html")

#define PROFILE_DURATION 20

#define EVENT_POOL_SIZE 7268

#define FD_CACHE_SIZE 256

#define URING_ENTRIES        2048
#define URING_SQ_LEAVE_SPACE 10
#define URING_SQE_RETRY_MAX  128

#define CONNECTION_POOL_SIZE 1280

#ifdef DEBUG_BUILD
#define CONNECTION_TIMEOUT 6000
#else
#define CONNECTION_TIMEOUT 60
#endif

#define RECV_BUF_INITIAL_CAPACITY 2048
#define RECV_BUF_MAX_CAPACITY     10240
#define SEND_BUF_INITIAL_CAPACITY 2048
#define SEND_BUF_MAX_CAPACITY     20480

#define HTTP_ERROR_BODY_MAX_LENGTH      512
#define HTTP_REQUEST_LINE_MAX_LENGTH    2048
#define HTTP_REQUEST_HEADER_MAX_LENGTH  2048
#define HTTP_REQUEST_HOST_MAX_LENGTH    512
#define HTTP_REQUEST_URI_MAX_LENGTH     512
#define HTTP_REQUEST_CONTENT_MAX_LENGTH 10240

#define HTTP_TIME_FORMAT     "%a, %d %b %Y %H:%M:%S GMT"
#define HTTP_TIME_BUF_LENGTH 30
#define HTTP_TIME_CACHE      13
