#pragma once

#include <assert.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>

#include "forward.h"
#include "types.h"

static const uintptr_t EVENT_PTR_IGNORE = 1;

#define EVENT_TYPE_ENUM                                                        \
    _EVENT_TYPE(INVALID_EVENT)                                                 \
    _EVENT_TYPE(ACCEPT)                                                        \
    _EVENT_TYPE(SEND)                                                          \
    _EVENT_TYPE(RECV)                                                          \
    _EVENT_TYPE(CLOSE)

enum EventType {
#define _EVENT_TYPE(E) E,
    EVENT_TYPE_ENUM
#undef _EVENT_TYPE
};

const char* event_type_name(enum EventType);

struct Event {
    enum EventType type;
};

struct io_uring event_init();

bool event_accept_submit(struct Event*, struct io_uring*, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len);
bool event_send_submit(struct Event*, struct io_uring*, fd socket,
                       const void* buf, size_t len, int flags);
bool event_recv_submit(struct Event*, struct io_uring*, fd socket,
                       void* out_buf, size_t buf_len);
bool event_close_submit(struct Event*, struct io_uring*, fd socket);
