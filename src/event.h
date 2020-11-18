#pragma once

#include <liburing.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "ptr.h"
#include "socket.h"

static const uintptr_t EVENT_PTR_IGNORE = 1;

#define EVENT_TYPE_ENUM                                                        \
    _EVENT_TYPE(INVALID_EVENT)                                                 \
    _EVENT_TYPE(ACCEPT)                                                        \
    _EVENT_TYPE(SEND)                                                          \
    _EVENT_TYPE(RECV)                                                          \
    _EVENT_TYPE(CLOSE)                                                         \
    _EVENT_TYPE(TIMEOUT)

typedef enum EventType {
#define _EVENT_TYPE(E) E,
    EVENT_TYPE_ENUM
#undef _EVENT_TYPE
} EventType;

CString event_type_name(EventType);

typedef struct Event {
    EventType type;
} Event;

struct io_uring event_init(void);

bool event_accept_submit(Event*, struct io_uring*, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len);
bool event_send_submit(Event*, struct io_uring*, fd socket, CString data,
                       unsigned sqe_flags);
bool event_recv_submit(Event*, struct io_uring*, fd socket, String out_data);
bool event_read_submit(Event*, struct io_uring*, fd file, String out_data,
                       size_t nbytes, off_t offset, unsigned sqe_flags);
bool event_close_submit(Event*, struct io_uring*, fd socket);
bool event_timeout_submit(Event*, struct io_uring*, time_t sec, time_t nsec);
