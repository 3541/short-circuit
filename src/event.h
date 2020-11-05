#pragma once

#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

#include "forward.h"
#include "ptr.h"
#include "socket.h"

static const uintptr_t EVENT_PTR_IGNORE = 1;

#define EVENT_TYPE_ENUM                                                        \
    _EVENT_TYPE(INVALID_EVENT)                                                 \
    _EVENT_TYPE(ACCEPT)                                                        \
    _EVENT_TYPE(SEND)                                                          \
    _EVENT_TYPE(RECV)                                                          \
    _EVENT_TYPE(CLOSE)

typedef enum EventType {
#define _EVENT_TYPE(E) E,
    EVENT_TYPE_ENUM
#undef _EVENT_TYPE
} EventType;

CString event_type_name(EventType);

typedef struct Event {
    EventType type;
} Event;

struct io_uring event_init();

bool event_accept_submit(Event*, struct io_uring*, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len);
bool event_send_submit(Event*, struct io_uring*, fd socket, CByteString data,
                       unsigned sqe_flags);
bool event_recv_submit(Event*, struct io_uring*, fd socket,
                       ByteString out_data);
bool event_close_submit(Event*, struct io_uring*, fd socket);
