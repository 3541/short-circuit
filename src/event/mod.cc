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
#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <memory>
#include <string.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/log.h>
#include <a3/pool.h>
#include <a3/sll.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "event/handle.h"
#include "event/internal.hh"

using std::move;
using std::unique_ptr;

SLL_DEFINE_METHODS(Event);

static Pool* EVENT_POOL = nullptr;

Event::Event(EventTarget* tgt, EventType ty, bool chain, bool ignore,
             bool queue) :
    type { ty } {
    target_ptr = reinterpret_cast<uintptr_t>(tgt) | (chain ? EVENT_CHAIN : 0) |
                 (ignore ? EVENT_IGNORE : 0);
    if (queue)
        SLL_PUSH(Event)(tgt, this);
}

void* Event::operator new(size_t size) noexcept {
    assert(size == sizeof(Event));
    (void)size;

    void* ret = pool_alloc_block(EVENT_POOL);
    if (!ret) {
        log_msg(WARN, "Event pool exhausted.");
        return nullptr;
    }

    return ret;
}

void Event::operator delete(void* event) {
    assert(event);

    pool_free_block(EVENT_POOL, event);
}

CString event_type_name(EventType ty) {
#define _EVENT_TYPE(E) [E] = CS(#E),
    static const CString EVENT_NAMES[] = { EVENT_TYPE_ENUM };
#undef _EVENT_TYPE

    if (!(0 <= ty && static_cast<size_t>(ty) <
                         sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0])))
        return CS("INVALID");

    return EVENT_NAMES[ty];
}

// Check that the kernel is recent enough to support io_uring and
// io_uring_probe.
static void event_check_kver(void) {
    struct utsname info;
    UNWRAPSD(uname(&info));

    char* release = strdup(info.release);

    long version_major = strtol(strtok(info.release, "."), nullptr, 10);
    long version_minor = strtol(strtok(nullptr, "."), nullptr, 10);

    if (version_major < MIN_KERNEL_VERSION_MAJOR ||
        (version_major == MIN_KERNEL_VERSION_MAJOR &&
         version_minor < MIN_KERNEL_VERSION_MINOR))
        PANIC_FMT(
            "Kernel version %s is not supported. At least %d.%d is required.",
            release, MIN_KERNEL_VERSION_MAJOR, MIN_KERNEL_VERSION_MINOR);

    free(release);
}

#define REQUIRE_OP(P, OP)                                                      \
    do {                                                                       \
        if (!io_uring_opcode_supported(P, OP))                                 \
            PANIC_FMT(                                                         \
                "Required io_uring op %s is not supported by the kernel.",     \
                #OP);                                                          \
    } while (0)

// All ops used should be checked here.
static void event_check_ops(struct io_uring* uring) {
    struct io_uring_probe* probe = io_uring_get_probe_ring(uring);

    REQUIRE_OP(probe, IORING_OP_ACCEPT);
    REQUIRE_OP(probe, IORING_OP_ASYNC_CANCEL);
    REQUIRE_OP(probe, IORING_OP_CLOSE);
    REQUIRE_OP(probe, IORING_OP_READ);
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_SEND);
    REQUIRE_OP(probe, IORING_OP_SPLICE);
    REQUIRE_OP(probe, IORING_OP_TIMEOUT);

    free(probe);
}

// Set the given resource to its hard limit and return the new state.
static struct rlimit rlimit_maximize(int resource) {
    struct rlimit lim;

    UNWRAPSD(getrlimit(resource, &lim));
    lim.rlim_cur = lim.rlim_max;
    UNWRAPSD(setrlimit(resource, &lim));
    return lim;
}

static void event_check_rlimits(void) {
    struct rlimit lim_memlock = rlimit_maximize(RLIMIT_MEMLOCK);

    // This is a crude check, but opening the queue will almost certainly fail
    // if the limit is this low.
    if (lim_memlock.rlim_cur <= 96 * URING_ENTRIES)
        log_fmt(
            WARN,
            "The memlock limit (%d) is too low. The queue will probably "
            "fail to open. Either raise the limit or lower `URING_ENTRIES`.",
            lim_memlock.rlim_cur);

    struct rlimit lim_nofile = rlimit_maximize(RLIMIT_NOFILE);
    if (lim_nofile.rlim_cur <= CONNECTION_POOL_SIZE * 3)
        log_fmt(
            WARN,
            "The open file limit (%d) is low. Large numbers of concurrent "
            "connections will probably cause \"too many open files\" errors.",
            lim_nofile.rlim_cur);
}

struct io_uring event_init() {
    event_check_kver();
    event_check_rlimits();

    struct io_uring ret;

    bool opened = false;
    for (size_t queue_size = URING_ENTRIES; queue_size >= 512;
         queue_size /= 2) {
        if (!io_uring_queue_init(URING_ENTRIES, &ret, 0)) {
            opened = true;
            break;
        }
    }
    if (!opened)
        PANIC("Unable to open queue. The memlock limit is probably too low.");

    event_check_ops(&ret);

    EVENT_POOL = POOL_OF(Event, EVENT_POOL_SIZE, POOL_ZERO_BLOCKS, nullptr);

    return ret;
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it
// is full. This /can/ return a null pointer if the SQ is full and, for whatever
// reason, it does not empty in time.
static unique_ptr<struct io_uring_sqe> event_get_sqe(struct io_uring& uring) {
    struct io_uring_sqe* ret = io_uring_get_sqe(&uring);
    // Try to service events until an SQE is available or too many retries have
    // elapsed.
    for (size_t retries = 0; !ret && retries < URING_SQE_RETRY_MAX;
         ret            = io_uring_get_sqe(&uring), retries++)
        if (io_uring_submit(&uring) < 0)
            break;
    if (!ret)
        log_msg(WARN, "SQ full.");
    return unique_ptr<struct io_uring_sqe> { ret };
}

static void event_sqe_fill(unique_ptr<Event>&&               event,
                           unique_ptr<struct io_uring_sqe>&& sqe,
                           uint32_t                          sqe_flags = 0) {
    io_uring_sqe_set_flags(sqe.get(), sqe_flags);
    io_uring_sqe_set_data(sqe.release(), event.release());
}

bool event_accept_submit(EventTarget* target, struct io_uring* uring, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len) {
    assert(target);
    assert(uring);
    assert(out_client_addr);

    auto event = Event::create(target, EVENT_ACCEPT);
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_accept(sqe.get(), socket,
                         reinterpret_cast<struct sockaddr*>(out_client_addr),
                         inout_addr_len, 0);
    event_sqe_fill(move(event), move(sqe), 0);

    return true;
}

static bool event_close_fallback(Event* event, struct io_uring& uring,
                                 fd file) {
    assert(file >= 0);

    if (close(file) != 0)
        return false;

    if (event)
        event->handle(uring);

    return true;
}

bool event_close_submit(EventTarget* target, struct io_uring* uring, fd file,
                        uint32_t sqe_flags, bool fallback_sync) {
    assert(uring);
    assert(file >= 0);

    unique_ptr<Event> event;

    if (target) {
        event = Event::create(target, EVENT_CLOSE);
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

bool event_openat_submit(EventTarget* target, struct io_uring* uring, fd dir,
                         CString path, int32_t open_flags, mode_t mode) {
    assert(target);
    assert(uring);
    assert(path.ptr);

    auto event = Event::create(target, EVENT_OPENAT);
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_openat(sqe.get(), dir,
                         reinterpret_cast<const char*>(path.ptr), open_flags,
                         mode);
    event_sqe_fill(move(event), move(sqe));

    return true;
}

bool event_read_submit(EventTarget* target, struct io_uring* uring, fd file,
                       String out_data, size_t nbytes, off_t offset,
                       uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(file >= 0);
    assert(out_data.ptr);

    auto event = Event::create(target, EVENT_READ,
                               sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK));
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_read(sqe.get(), file, out_data.ptr,
                       static_cast<uint32_t>(MIN(out_data.len, nbytes)),
                       offset);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_recv_submit(EventTarget* target, struct io_uring* uring, fd socket,
                       String data) {
    assert(target);
    assert(uring);
    assert(data.ptr);

    auto event = Event::create(target, EVENT_RECV);
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_recv(sqe.get(), socket, data.ptr, data.len, 0);
    event_sqe_fill(move(event), move(sqe));

    return true;
}

bool event_send_submit(EventTarget* target, struct io_uring* uring, fd socket,
                       CString data, uint32_t send_flags, uint32_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(data.ptr);

    auto event = Event::create(target, EVENT_SEND,
                               sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK));
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_send(sqe.get(), socket, data.ptr, data.len, (int)send_flags);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_splice_submit(EventTarget* target, struct io_uring* uring, fd in,
                         uint64_t off_in, fd out, size_t len,
                         uint32_t splice_flags, uint32_t sqe_flags,
                         bool ignore) {
    assert(target);
    assert(uring);
    assert(in >= 0);
    assert(out >= 0);

    auto event =
        Event::create(target, EVENT_SPLICE,
                      sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK), ignore);
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_splice(sqe.get(), in, off_in, out, (uint64_t)-1,
                         (unsigned)len, SPLICE_F_MOVE | splice_flags);
    event_sqe_fill(move(event), move(sqe), sqe_flags);

    return true;
}

bool event_timeout_submit(EventTarget* target, struct io_uring* uring,
                          Timespec* threshold, uint32_t timeout_flags) {
    assert(target);
    assert(uring);
    assert(threshold);

    auto event = Event::create(target, EVENT_TIMEOUT);
    TRYB(event);

    auto sqe = event_get_sqe(*uring);
    TRYB(sqe);

    io_uring_prep_timeout(sqe.get(), threshold, 0, timeout_flags);
    event_sqe_fill(move(event), move(sqe), 0);

    return true;
}

bool event_cancel_all(EventTarget* target) {
    assert(target);

    for (Event* victim = SLL_POP(Event)(target); victim;
         victim        = SLL_POP(Event)(target))
        victim->cancel();

    return true;
}

Event* event_create(EventTarget* target, EventType ty) {
    assert(target);
    return Event::create(target, ty, /* chain */ false, /* ignore */ false,
                         /* queue */ false)
        .release();
}
