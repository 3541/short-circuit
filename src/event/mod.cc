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
#include "event/internal.h"
#include "forward.h"

using std::move;
using std::unique_ptr;

A3Pool* EVENT_POOL = A3_POOL_OF(Event, EVENT_POOL_SIZE, A3_POOL_ZERO_BLOCKS, NULL, NULL);

static Event* event_new(EventTarget* target, EventHandler handler, void* handler_ctx,
                        int32_t expected_return, bool queue) {
    Event* event = (Event*)a3_pool_alloc_block(EVENT_POOL);

    event->success         = true;
    event->expected_return = expected_return;
    event->target          = target;
    event->handler         = handler;
    event->handler_ctx     = handler_ctx;

    if (queue)
        a3_sll_push(target, &event->queue_link);

    return event;
}

Event* event_from_link(A3SLink* link) {
    if (!link)
        return NULL;
    return A3_CONTAINER_OF(link, Event, queue_link);
}

void event_free(Event* event) {
    assert(event);

    a3_pool_free_block(EVENT_POOL, event);
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it is full. This /can/
// return a null pointer if the SQ is full and, for whatever reason, it does not empty in time.
static struct io_uring_sqe* event_get_sqe(struct io_uring* uring) {
    struct io_uring_sqe* ret = io_uring_get_sqe(uring);
    // Try to submit events until an SQE is available or too many retries have elapsed.
    for (size_t retries = 0; !ret && retries < URING_SQE_RETRY_MAX;
         ret            = io_uring_get_sqe(uring), retries++)
        if (io_uring_submit(uring) < 0)
            break;
    if (!ret)
        a3_log_msg(LOG_WARN, "SQ full.");
    return ret;
}

static bool event_submit(EventTarget* target, struct io_uring_sqe* sqe, EventHandler handler,
                         void* handler_ctx, int32_t expected_return, bool queue) {
    Event* event = event_new(target, handler, handler_ctx, expected_return, queue);
    A3_TRYB(event);
    io_uring_sqe_set_data(sqe, event);
    return true;
}

bool event_accept_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd socket, struct sockaddr_in* out_client_addr,
                         socklen_t* inout_addr_len) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(out_client_addr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_accept(sqe, socket, reinterpret_cast<struct sockaddr*>(out_client_addr),
                         inout_addr_len, 0);
    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_NONNEGATIVE, true);
}

static bool event_close_fallback(EventTarget* target, EventHandler handler, struct io_uring* uring,
                                 void* handler_ctx, fd file) {
    assert(file >= 0);

    int32_t status  = close(file);
    bool    success = status == 0;

    if (handler)
        handler(target, uring, handler_ctx, success, status);

    return success;
}

bool event_close_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                        void* handler_ctx, fd file, uint32_t sqe_flags, bool fallback_sync) {
    assert(uring);
    assert(file >= 0);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    if (!sqe && fallback_sync)
        return event_close_fallback(target, handler, uring, handler_ctx, file);

    io_uring_prep_close(sqe, file);
    io_uring_sqe_set_flags(sqe, sqe_flags);

    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_NONNEGATIVE, true);
}

bool event_openat_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd dir, A3CString path, int32_t open_flags,
                         mode_t mode) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(path.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_openat(sqe, dir, a3_string_cstr(path), open_flags, mode);

    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_NONNEGATIVE, true);
}

bool event_read_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd file, A3String out_data, size_t nbytes, off_t offset,
                       uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(file >= 0);
    assert(out_data.ptr);
    assert(offset >= 0);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    uint32_t read_size = (uint32_t)MIN(out_data.len, nbytes);
    io_uring_prep_read(sqe, file, out_data.ptr, read_size, (uint64_t)offset);
    io_uring_sqe_set_flags(sqe, sqe_flags);

    return event_submit(target, sqe, handler, handler_ctx, (int32_t)read_size, true);
}

bool event_recv_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd socket, A3String data) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(socket >= 0);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_recv(sqe, socket, data.ptr, data.len, 0);

    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_POSITIVE, true);
}

bool event_send_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                       void* handler_ctx, fd socket, A3CString data, uint32_t send_flags,
                       uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(socket >= 0);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_send(sqe, socket, data.ptr, data.len, (int32_t)send_flags);
    io_uring_sqe_set_flags(sqe, sqe_flags);

    return event_submit(target, sqe, handler, handler_ctx, (int32_t)data.len, true);
}

bool event_splice_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                         void* handler_ctx, fd in, uint64_t off_in, fd out, size_t len,
                         uint32_t splice_flags, uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(in >= 0);
    assert(out >= 0);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_splice(sqe, in, (int64_t)off_in, out, -1, (uint32_t)len,
                         SPLICE_F_MOVE | splice_flags);
    io_uring_sqe_set_flags(sqe, sqe_flags);

    return event_submit(target, sqe, handler, handler_ctx, (int32_t)len, true);
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

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_statx(sqe, -1, a3_string_cstr(path), 0, field_mask, statx_buf);
    io_uring_sqe_set_flags(sqe, sqe_flags);

    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_NONNEGATIVE, true);
}

bool event_timeout_submit(EventTarget* target, struct io_uring* uring, EventHandler handler,
                          void* handler_ctx, Timespec* threshold, uint32_t timeout_flags) {
    assert(target);
    assert(uring);
    assert(handler);
    assert(threshold);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    A3_TRYB(sqe);

    io_uring_prep_timeout(sqe, threshold, 0, timeout_flags);

    return event_submit(target, sqe, handler, handler_ctx, EXPECTED_STATUS_NONE, true);
}

bool event_cancel_all(EventTarget* target) {
    assert(target);

    for (Event* victim = event_from_link(a3_sll_pop(target)); victim;
         victim        = event_from_link(a3_sll_pop(target)))
        victim->target = NULL;

    return true;
}

Event* event_create(EventTarget* target, EventHandler handler, void* handler_ctx) {
    assert(target);
    return event_new(target, handler, handler_ctx, EXPECTED_STATUS_NONE, EVENT_NO_QUEUE);
}

A3SLink* event_queue_link(Event* event) {
    assert(event);
    return &event->queue_link;
}
