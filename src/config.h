/*
 * SHORT CIRCUIT: CONFIG -- Configurable settings.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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

#include <a3/str.h>

#define SC_MIN_KERNEL_VERSION_MAJOR 5
#define SC_MIN_KERNEL_VERSION_MINOR 6

#ifndef SC_DEFAULT_LISTEN_PORT
#define SC_DEFAULT_LISTEN_PORT 8000
#endif
#ifndef SC_LISTEN_BACKLOG
#define SC_LISTEN_BACKLOG 1024
#endif
#ifndef SC_DEFAULT_WEB_ROOT
#define SC_DEFAULT_WEB_ROOT A3_CS(".")
#endif
#ifndef SC_INDEX_FILENAME
#define SC_INDEX_FILENAME A3_CS("index.html")
#endif

#ifndef SC_PROFILE_DURATION
#define SC_PROFILE_DURATION 20
#endif

#ifndef SC_URING_ENTRIES
#define SC_URING_ENTRIES 2048
#endif
#ifndef SC_URING_SQ_LEAVE_SPACE
#define SC_URING_SQ_LEAVE_SPACE 10
#endif
#ifndef SC_URING_SQE_RETRY_MAX
#define SC_URING_SQE_RETRY_MAX 128
#endif

#ifndef SC_CONNECTION_TIMEOUT
#ifdef NDEBUG
#define SC_CONNECTION_TIMEOUT 60
#else
#define SC_CONNECTION_TIMEOUT 6000
#endif
#endif

#ifndef SC_RECV_BUF_INIT_CAP
#define SC_RECV_BUF_INIT_CAP 2048
#endif
#ifndef SC_RECV_BUF_MAX_CAP
#define SC_RECV_BUF_MAX_CAP 10240
#endif
#ifndef SC_SEND_BUF_INIT_CAP
#define SC_SEND_BUF_INIT_CAP 2048
#endif
#ifndef SC_SEND_BUF_MAX_CAP
#define SC_SEND_BUF_MAX_CAP 20480
#endif
#ifndef SC_RECV_BUF_MIN_SPACE
#define SC_RECV_BUF_MIN_SPACE 512
#endif

#ifndef SC_CO_STACK_SIZE
#define SC_CO_STACK_SIZE 16384
#endif

#ifndef SC_CONNECTION_POOL_SIZE
#define SC_CONNECTION_POOL_SIZE 1280
#endif

#ifndef SC_HTTP_REQUEST_LINE_MAX_LENGTH
#define SC_HTTP_REQUEST_LINE_MAX_LENGTH 2048
#endif

#ifndef SC_HTTP_HEADER_MAX_LENGTH
#define SC_HTTP_HEADER_MAX_LENGTH 2048
#endif

#ifndef SC_HTTP_REQUEST_CONTENT_MAX_LENGTH
#define SC_HTTP_REQUEST_CONTENT_MAX_LENGTH 10240
#endif
