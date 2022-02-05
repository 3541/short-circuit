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

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace sc::config {

// This is the minimum kernel version which supports IORING_REGISTER_PROBE.
static constexpr size_t MIN_KERNEL_VERSION_MAJOR = 5;
static constexpr size_t MIN_KERNEL_VERSION_MINOR = 6;

static constexpr uint16_t DEFAULT_LISTEN_PORT = 8000;
static constexpr uint32_t LISTEN_BACKLOG      = 1024;

static constexpr char const* DEFAULT_WEB_ROOT = ".";
static constexpr char const* INDEX_FILENAME   = "index.html";

static constexpr std::chrono::duration<size_t> PROFILE_DURATION { 20 };

static constexpr size_t EVENT_POOL_SIZE      = 7268;
static constexpr size_t CONNECTION_POOL_SIZE = 1280;
static constexpr size_t FD_CACHE_SIZE        = 256;

static constexpr size_t URING_ENTRIES        = 2048;
static constexpr size_t URING_SQ_LEAVE_SPACE = 10;
static constexpr size_t URING_SQE_RETRY_MAX  = 128;

#ifndef NDEBUG
static constexpr std::chrono::duration<size_t> CONNECTION_TIMEOUT { 6000 };
#else
static constexpr std::chrono::duration<size_t> CONNECTION_TIMEOUT { 60 };
#endif

static constexpr size_t RECV_BUF_INITIAL_CAPACITY { 2048 };
static constexpr size_t RECV_BUF_MAX_CAPACITY { 10240 };
static constexpr size_t SEND_BUF_INITIAL_CAPACITY { 2048 };
static constexpr size_t SEND_BUF_MAX_CAPACITY { 20480 };

static constexpr size_t HTTP_ERROR_BODY_MAX_LENGTH { 512 };
static constexpr size_t HTTP_REQUEST_LINE_MAX_LENGTH { 2048 };
static constexpr size_t HTTP_REQUEST_HEADER_MAX_LENGTH { 2048 };
static constexpr size_t HTTP_REQUEST_HOST_MAX_LENGTH { 512 };
static constexpr size_t HTTP_REQUEST_URI_MAX_LENGTH { 512 };
static constexpr size_t HTTP_REQUEST_CONTENT_MAX_LENGTH { 10240 };

static constexpr char const* HTTP_TIME_FORMAT     = "%a, %d %b %Y %H:%M:%S GMT";
static constexpr size_t      HTTP_TIME_BUF_LENGTH = 30;
static constexpr size_t      HTTP_TIME_CACHE      = 13;

} // namespace sc::config
