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

// http_connection.h
struct HttpConnection;
typedef struct HttpConnection HttpConnection;

// listen.h
struct Listener;
typedef struct Listener Listener;

// socket.h
typedef int fd;
