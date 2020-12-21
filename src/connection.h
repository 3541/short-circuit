#pragma once

#include <liburing.h>
#include <liburing/io_uring.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include <a3/buffer.h>

#include "event.h"
#include "forward.h"
#include "socket.h"
#include "timeout.h"

// Callback types to submit events.
typedef bool (*ConnectionSubmit)(Connection*, struct io_uring*,
                                 uint32_t event_flags, uint8_t sqe_flags);
typedef bool (*ConnectionHandle)(Connection*, struct io_uring*, int32_t status,
                                 bool chain);

typedef enum ConnectionTransport {
    PLAIN,
    TLS,
    NTRANSPORTS
} ConnectionTransport;

typedef struct Connection {
    EVENT_TARGET;

    Listener* listener;

    ConnectionTransport transport;

    fd                 socket;
    struct sockaddr_in client_addr;
    socklen_t          addr_len;

    ConnectionSubmit recv_submit;
    ConnectionHandle recv_handle;

    ConnectionSubmit send_submit;

    Buffer recv_buf;
    Buffer send_buf;

    Timeout timeout;
} Connection;

void connection_timeout_init(void);

bool connection_init(Connection*);
bool connection_reset(Connection*, struct io_uring*);

Connection* connection_accept_submit(Listener*, struct io_uring*);
bool connection_send_submit(Connection*, struct io_uring*, uint32_t send_flags,
                            uint8_t sqe_flags);
bool connection_splice_submit(Connection*, struct io_uring*, fd src, size_t len,
                              uint8_t sqe_flags);
bool connection_close_submit(Connection*, struct io_uring*);

bool connection_accept_handle(Connection*, struct io_uring*, int32_t status,
                              bool chain);
bool connection_cancel_handle(Connection*, struct io_uring*, int32_t status,
                              bool chain);
bool connection_close_handle(Connection*, struct io_uring*, int32_t status,
                             bool chain);
bool connection_read_handle(Connection*, struct io_uring*, int32_t status,
                            bool chain);
bool connection_recv_handle(Connection*, struct io_uring*, int32_t status,
                            bool chain);
bool connection_send_handle(Connection*, struct io_uring*, int32_t status,
                            bool chain);
bool connection_splice_handle(Connection*, struct io_uring*, int32_t status,
                              bool chain);
void connection_event_handle(Connection*, struct io_uring*, EventType,
                             int32_t status, bool chain);
