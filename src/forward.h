/*
 * SHORT CIRCUIT: FORWARD -- Forward declarations.
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

// liburing.h
struct io_uring;
struct io_uring_cqe;

// netinet/in.h
struct sockaddr_in;

// buffer.h
struct Buffer;
typedef struct Buffer Buffer;

// connection.h
struct Connection;
typedef struct Connection Connection;

// event.h
struct Event;
typedef struct Event Event;

typedef struct EventSLL EventTarget;

// http_connection.h
struct HttpConnection;
typedef struct HttpConnection HttpConnection;

// http_request.h
struct HttpRequest;
typedef struct HttpRequest HttpRequest;

// http_response.h
struct HttpResponse;
typedef struct HttpResponse HttpResponse;

// listen.h
struct Listener;
typedef struct Listener Listener;

// socket.h
typedef int fd;

// timeout.h
typedef struct __kernel_timespec Timespec;
