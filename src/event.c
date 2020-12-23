#define _GNU_SOURCE // For SPLICE_F_MOVE from fcntl.
#include "event.h"

#include <assert.h>
#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/utsname.h>

#include <a3/log.h>
#include <a3/pool.h>
#include <a3/sll.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "event_internal.h"
#include "socket.h"

SLL_DEFINE_METHODS(Event);

static Pool* EVENT_POOL = NULL;

static Event* event_new(EventTarget* target, EventType ty, bool chain,
                        bool ignore) {
    assert(target);

    Event* ret = pool_alloc_block(EVENT_POOL);
    if (!ret) {
        log_msg(WARN, "Event pool exhausted.");
        return NULL;
    }

    ret->target = (EventTarget*)((uintptr_t)target | (chain ? EVENT_CHAIN : 0) |
                                 (ignore ? EVENT_IGNORE : 0));
    ret->type   = ty;
    SLL_PUSH(Event)(target, ret);

    return ret;
}

void event_free(Event* event) {
    assert(event);

    pool_free_block(EVENT_POOL, event);
}

CString event_type_name(EventType ty) {
#define _EVENT_TYPE(E) [E] = CS(#E),
    static const CString EVENT_NAMES[] = { EVENT_TYPE_ENUM };
#undef _EVENT_TYPE

    if (!(0 <= ty && ty < sizeof(EVENT_NAMES)))
        return CS("INVALID");

    return EVENT_NAMES[ty];
}

// Check that the kernel is recent enough to support io_uring and
// io_uring_probe.
static void event_check_kver(void) {
    struct utsname info;
    UNWRAPSD(uname(&info));

    char* release = strdup(info.release);

    long version_major = strtol(strtok(info.release, "."), NULL, 10);
    long version_minor = strtol(strtok(NULL, "."), NULL, 10);

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

struct io_uring event_init() {
    event_check_kver();

    struct io_uring ret;
    int rc;
    if ((rc = io_uring_queue_init(URING_ENTRIES, &ret, 0)) < 0) {
        log_error(-rc, "Failed to open queue.");
        PANIC("Unable to open queue.");
    }

    event_check_ops(&ret);

    EVENT_POOL = pool_new(sizeof(Event), EVENT_POOL_SIZE);

    return ret;
}

// Get an SQE. This may trigger a submission in an attempt to clear the SQ if it
// is full. This /can/ return NULL if the SQ is full and, for whatever reason,
// it does not empty in time.
static struct io_uring_sqe* event_get_sqe(struct io_uring* uring) {
    struct io_uring_sqe* ret = io_uring_get_sqe(uring);
    // Try to service events until an SQE is available or too many retries have
    // elapsed.
    for (size_t retries = 0; !ret && retries < URING_SQE_RETRY_MAX;
         ret            = io_uring_get_sqe(uring), retries++)
        if (io_uring_submit(uring) < 0)
            break;
    if (!ret)
        log_msg(WARN, "SQ full.");
    return ret;
}

static void event_sqe_fill(Event* this, struct io_uring_sqe* sqe,
                           uint8_t sqe_flags) {
    assert(sqe);

    io_uring_sqe_set_flags(sqe, sqe_flags);
    io_uring_sqe_set_data(sqe, this);
}

bool event_accept_submit(EventTarget* target, struct io_uring* uring, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len) {
    assert(target);
    assert(uring);
    assert(out_client_addr);

    Event* event = event_new(target, EVENT_ACCEPT, false, false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_accept(sqe, socket, (struct sockaddr*)out_client_addr,
                         inout_addr_len, 0);
    event_sqe_fill(event, sqe, 0);

    return true;
}

bool event_send_submit(EventTarget* target, struct io_uring* uring, fd socket,
                       CString data, uint32_t send_flags, uint8_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(data.ptr);

    Event* event =
        event_new(target, EVENT_SEND,
                  sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK), false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_send(sqe, socket, data.ptr, data.len, (int)send_flags);
    event_sqe_fill(event, sqe, sqe_flags);

    return true;
}

bool event_splice_submit(EventTarget* target, struct io_uring* uring, fd in,
                         fd out, size_t len, uint8_t sqe_flags, bool ignore) {
    assert(target);
    assert(uring);
    assert(in >= 0);
    assert(out >= 0);

    Event* event =
        event_new(target, EVENT_SPLICE,
                  sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK), ignore);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_splice(sqe, in, (uint64_t)-1, out, (uint64_t)-1,
                         (unsigned)len, SPLICE_F_MOVE);
    event_sqe_fill(event, sqe, sqe_flags);

    return true;
}

bool event_recv_submit(EventTarget* target, struct io_uring* uring, fd socket,
                       String data) {
    assert(target);
    assert(uring);
    assert(data.ptr);

    Event* event = event_new(target, EVENT_RECV, false, false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_recv(sqe, socket, data.ptr, data.len, 0);
    event_sqe_fill(event, sqe, 0);

    return true;
}

bool event_read_submit(EventTarget* target, struct io_uring* uring, fd file,
                       String out_data, size_t nbytes, off_t offset,
                       uint8_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(file >= 0);
    assert(out_data.ptr);

    Event* event =
        event_new(target, EVENT_READ,
                  sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK), false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_read(sqe, file, out_data.ptr,
                       (uint32_t)MIN(out_data.len, nbytes), offset);
    event_sqe_fill(event, sqe, sqe_flags);

    return true;
}

bool event_close_submit(EventTarget* target, struct io_uring* uring, fd socket,
                        uint8_t sqe_flags) {
    assert(uring);

    Event* event = NULL;

    if (target) {
        event = event_new(target, EVENT_CLOSE, false, false);
        TRYB(event);
    }

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_close(sqe, socket);
    event_sqe_fill(event, sqe, sqe_flags);

    return true;
}

bool event_timeout_submit(EventTarget* target, struct io_uring* uring,
                          Timespec* threshold, uint32_t timeout_flags) {
    assert(target);
    assert(uring);
    assert(threshold);

    Event* event = event_new(target, EVENT_TIMEOUT, false, false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_timeout(sqe, threshold, 0, timeout_flags);
    event_sqe_fill(event, sqe, 0);

    return true;
}

bool event_cancel_submit(EventTarget* target, struct io_uring* uring,
                         Event* victim, uint8_t sqe_flags) {
    assert(target);
    assert(uring);
    assert(victim);

    Event* event =
        event_new(target, EVENT_CANCEL,
                  sqe_flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK), false);
    TRYB(event);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_cancel(sqe, victim, 0);
    event_sqe_fill(event, sqe, sqe_flags);

    return true;
}

bool event_cancel_all(EventTarget* target, struct io_uring* uring,
                      uint8_t sqe_flags) {
    assert(target);
    assert(uring);

    for (Event* victim = SLL_POP(Event)(target); victim;
         victim        = SLL_POP(Event)(target))
        TRYB(event_cancel_submit(target, uring, victim, sqe_flags));

    return true;
}
