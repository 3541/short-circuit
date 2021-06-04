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

#include "event.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <liburing.h>
#include <memory>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

#include <a3/log.h>
#include <a3/pool.h>
#include <a3/sll.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "event/internal.hh"
#include "forward.h"

using std::move;
using std::unique_ptr;

using ExpectedStatus = Event::ExpectedStatus;

A3_POOL_STORAGE(Event, EVENT_POOL_SIZE, A3_POOL_ZERO_BLOCKS, nullptr);
A3_SLL_DEFINE_METHODS(Event);

Event::Event(EventTarget* tgt, EventHandler h, void* h_ctx, int32_t expected, bool queue) :
    status { expected }, target { tgt }, handler { h }, handler_ctx { h_ctx } {
    if (queue)
        A3_SLL_PUSH(Event)(tgt, this);
}

// It is expected that there should not be a situation which causes an sqe_ptr to be destroyed. If
// this does actually happen, log the error.
static void sqe_lost(void*) { a3_log_msg(LOG_ERROR, "Lost an sqe_ptr."); }

using sqe_ptr = unique_ptr<struct io_uring_sqe, decltype(sqe_lost)&>;

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it
// is full. This /can/ return a null pointer if the SQ is full and, for whatever
// reason, it does not empty in time.
static inline sqe_ptr event_get_sqe(struct io_uring& uring) {
    struct io_uring_sqe* ret = io_uring_get_sqe(&uring);
    // Try to service events until an SQE is available or too many retries have
    // elapsed.
    for (size_t retries = 0; !ret && retries < URING_SQE_RETRY_MAX;
         ret            = io_uring_get_sqe(&uring), retries++)
        if (io_uring_submit(&uring) < 0)
            break;
    if (!ret)
        a3_log_msg(LOG_WARN, "SQ full.");
    return sqe_ptr { ret, sqe_lost };
}

static inline void event_sqe_fill(unique_ptr<Event>&& event, sqe_ptr&& sqe,
                                  uint32_t sqe_flags = 0) {
    io_uring_sqe_set_flags(sqe.get(), sqe_flags);
    io_uring_sqe_set_data(sqe.release(), event.release());
}

bool event_accept_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd socket, struct sockaddr_in* out_client_addr,
                         socklen_t* inout_addr_len) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(out_client_addr);

    auto event = Event::create(target, handler, handler_ctx, ExpectedStatus::Nonnegative);
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_accept(sqe.get(), socket, reinterpret_cast<struct sockaddr*>(out_client_addr),
                         inout_addr_len, 0);
    event_sqe_fill(move(event), move(sqe), 0);

    return true;
}

static bool event_close_fallback(Event* event, struct io_uring& uring, fd file) {
    assert(file >= 0);

    if (close(file) != 0)
        return false;

    if (event)
        event->handle(uring);

    return true;
}

bool event_close_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                        void* handler_ctx, fd file, uint32_t sqe_flags, bool fallback_sync) {
    assert(uring);
    assert(file >= 0);

    unique_ptr<Event> event;

    if (target) {
        event = Event::create(target, handler, handler_ctx, ExpectedStatus::Nonnegative);
        if (!event) {
            if (fallback_sync)
                return event_close_fallback(nullptr, *uring, file);
            else
                return false;
        }
    }

    auto sqe = event_get_sqe(*uring);
    if (!sqe) {
        if (fallback_sync)
            return event_close_fallback(event.get(), *uring, file);
        else
            return false;
    }

    io_uring_prep_close(sqe.get(), file);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_openat_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd dir, A3CString path, int32_t open_flags,
                         mode_t mode) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(path.ptr);

    auto event = Event::create(target, handler, handler_ctx, ExpectedStatus::Nonnegative);
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_openat(sqe.get(), dir, reinterpret_cast<const char*>(path.ptr), open_flags, mode);
    event_sqe_fill(move(event), move(sqe));

    return true;
}

bool event_read_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd file, A3String out_data, size_t nbytes, off_t offset,
                       uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(file >= 0);
    assert(out_data.ptr);

    auto read_size = static_cast<uint32_t>(MIN(out_data.len, nbytes));

    auto event = Event::create(target, handler, handler_ctx, static_cast<int32_t>(read_size));
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_read(sqe.get(), file, out_data.ptr, read_size, offset);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_recv_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd socket, A3String data) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(socket >= 0);
    assert(data.ptr);

    auto event = Event::create(target, handler, handler_ctx, ExpectedStatus::Positive);
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_recv(sqe.get(), socket, data.ptr, data.len, 0);
    event_sqe_fill(move(event), move(sqe));

    return true;
}

bool event_send_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd socket, A3CString data, uint32_t send_flags,
                       uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(socket >= 0);
    assert(data.ptr);

    auto event = Event::create(target, handler, handler_ctx, static_cast<int32_t>(data.len));
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_send(sqe.get(), socket, data.ptr, data.len, static_cast<int32_t>(send_flags));
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_splice_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd in, uint64_t off_in, fd out, size_t len,
                         uint32_t splice_flags, uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(in >= 0);
    assert(out >= 0);

    auto event = Event::create(target, handler, handler_ctx, static_cast<int32_t>(len));
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_splice(sqe.get(), in, off_in, out, static_cast<uint64_t>(-1),
                         static_cast<uint32_t>(len), SPLICE_F_MOVE | splice_flags);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_stat_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, A3CString path, uint32_t field_mask,
                       struct statx* statx_buf, uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(path.ptr);
    assert(field_mask);
    assert(statx_buf);

    auto event = Event::create(target, handler, handler_ctx, ExpectedStatus::Nonnegative);
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_statx(sqe.get(), -1, A3_S_AS_C_STR(path), 0, field_mask, statx_buf);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_timeout_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                          void* handler_ctx, Timespec* threshold, uint32_t timeout_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(threshold);

    auto event = Event::create(target, handler, handler_ctx, ExpectedStatus::None);
    A3_TRYB(event);

    auto sqe = event_get_sqe(*uring);
    A3_TRYB(sqe);

    io_uring_prep_timeout(sqe.get(), threshold, 0, timeout_flags);
    event_sqe_fill(move(event), move(sqe), 0);

    return true;
}

bool event_cancel_all(EventTarget* target) {
    assert(target);

    for (Event* victim = A3_SLL_POP(Event)(target); victim; victim = A3_SLL_POP(Event)(target))
        victim->cancel();

    return true;
}

Event* event_create(EventTarget* target, EventHandler handler, void* handler_ctx) {
    assert(target);
    return Event::create(target, handler, handler_ctx, ExpectedStatus::None, EVENT_NO_QUEUE)
        .release();
}
