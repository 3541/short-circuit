/*
 * SHORT CIRCUIT: EVENT -- Event submission.
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

#include <liburing.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#include <a3/cpp.h>
#include <a3/platform/util.h>
#include <a3/sll.h>
#include <a3/str.h>

#include "forward.h"

A3_H_BEGIN

#define EVENT_FALLBACK_ALLOW  true
#define EVENT_FALLBACK_FORBID false

#define EVENT_TYPE_ENUM                                                                            \
    _EVENT_TYPE(EVENT_ACCEPT)                                                                      \
    _EVENT_TYPE(EVENT_CLOSE)                                                                       \
    _EVENT_TYPE(EVENT_OPENAT)                                                                      \
    _EVENT_TYPE(EVENT_OPENAT_SYNTH)                                                                \
    _EVENT_TYPE(EVENT_READ)                                                                        \
    _EVENT_TYPE(EVENT_RECV)                                                                        \
    _EVENT_TYPE(EVENT_SEND)                                                                        \
    _EVENT_TYPE(EVENT_SPLICE)                                                                      \
    _EVENT_TYPE(EVENT_STAT)                                                                        \
    _EVENT_TYPE(EVENT_TIMEOUT)                                                                     \
    _EVENT_TYPE(EVENT_INVALID)

typedef enum EventType {
#define _EVENT_TYPE(E) E,
    EVENT_TYPE_ENUM
#undef _EVENT_TYPE
} EventType;

typedef struct Event Event;

A3_SLL_DEFINE_STRUCTS(Event);
A3_SLL_DECLARE_METHODS(Event);

typedef A3_SLL(Event) EventTarget;

// Include this as a member to make an object a viable event target.
#define EVENT_TARGET   EventTarget _events_queued
#define EVT(O)         (&(O)->_events_queued)
#define EVT_PTR(T, TY) A3_CONTAINER_OF((T), TY, _events_queued)

A3CString event_type_name(EventType);

struct io_uring event_init(void);

bool event_accept_submit(EventTarget*, struct io_uring*, fd socket,
                         struct sockaddr_in* out_client_addr, socklen_t* inout_addr_len);
bool event_close_submit(EventTarget*, struct io_uring*, fd file, uint32_t sqe_flags,
                        bool fallback_sync);
bool event_openat_submit(EventTarget*, struct io_uring*, fd dir, A3CString path, int32_t open_flags,
                         mode_t mode);
bool event_read_submit(EventTarget*, struct io_uring*, fd file, A3String out_data, size_t nbytes,
                       off_t offset, uint32_t sqe_flags);
bool event_recv_submit(EventTarget*, struct io_uring*, fd socket, A3String out_data);
bool event_send_submit(EventTarget*, struct io_uring*, fd socket, A3CString data,
                       uint32_t send_flags, uint32_t sqe_flags);
bool event_splice_submit(EventTarget*, struct io_uring*, fd in, uint64_t off_in, fd out, size_t len,
                         uint32_t splice_flags, uint32_t sqe_flags, bool force_handle);
bool event_stat_submit(EventTarget*, struct io_uring*, A3CString path, uint32_t field_mask,
                       struct statx*, uint32_t sqe_flags);
bool event_timeout_submit(EventTarget*, struct io_uring*, Timespec*, uint32_t timeout_flags);

// Synthesize an event. This Event is _not_ queued, but is useful for situations
// in which one completion from the uring must notify multiple targets. See
// file.c for an example of usage.
Event* event_create(EventTarget*, EventType);

bool event_cancel_all(EventTarget*);

A3_H_END
