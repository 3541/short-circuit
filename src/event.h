#pragma once

#include <liburing.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include "forward.h"
#include "ptr.h"
#include "socket.h"

#define EVENT_IGNORE_FLAG 1ULL
#define EVENT_IGNORE_DATA ((void*)EVENT_IGNORE_FLAG)

#define EVENT_TYPE_ENUM                                                        \
    _EVENT_TYPE(EVENT_ACCEPT)                                                  \
    _EVENT_TYPE(EVENT_CANCEL)                                                  \
    _EVENT_TYPE(EVENT_CLOSE)                                                   \
    _EVENT_TYPE(EVENT_INVALID)                                                 \
    _EVENT_TYPE(EVENT_RECV)                                                    \
    _EVENT_TYPE(EVENT_SEND)                                                    \
    _EVENT_TYPE(EVENT_TIMEOUT)

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
                       uint8_t sqe_flags);
bool event_recv_submit(Event*, struct io_uring*, fd socket, String out_data);
bool event_read_submit(Event*, struct io_uring*, fd file, String out_data,
                       size_t nbytes, off_t offset, uint8_t sqe_flags);
bool event_close_submit(Event*, struct io_uring*, fd socket);
bool event_timeout_submit(Event*, struct io_uring*, Timespec*,
                          uint32_t timeout_flags);
bool event_cancel_submit(Event*, struct io_uring*, Event* target,
                         uint8_t sqe_flags);
