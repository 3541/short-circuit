#include "event.h"

#include <assert.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/utsname.h>

#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "socket.h"

CString event_type_name(EventType ty) {
#define _EVENT_TYPE(E) { E, CS(#E) },
    static const struct {
        EventType ty;
        CString   name;
    } EVENT_NAMES[] = { EVENT_TYPE_ENUM };
#undef _EVENT_TYPE

    for (size_t i = 0; i < sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0]); i++) {
        if (ty == EVENT_NAMES[i].ty)
            return EVENT_NAMES[i].name;
    }

    return CS("INVALID");
}

// Check that the kernel is recent enough to support io_uring and
// io_uring_probe.
static void event_check_kver(void) {
    struct utsname info;
    UNWRAPSD(uname(&info));

    char* release = strdup(info.release);

    uint8_t version_major = strtol(strtok(info.release, "."), NULL, 10);
    uint8_t version_minor = strtol(strtok(NULL, "."), NULL, 10);

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
    REQUIRE_OP(probe, IORING_OP_TIMEOUT);

    free(probe);
}

struct io_uring event_init() {
    event_check_kver();

    struct io_uring ret;
    UNWRAPSD(io_uring_queue_init(URING_ENTRIES, &ret, 0));

    event_check_ops(&ret);

    return ret;
}

static struct io_uring_sqe* event_get_sqe(struct io_uring* uring) {
    struct io_uring_sqe* ret = io_uring_get_sqe(uring);
    if (!ret)
        log_msg(WARN, "SQ full.");
    return ret;
}

static void event_sqe_fill(Event* this, struct io_uring_sqe* sqe,
                           uint8_t sqe_flags) {
    assert(this);
    assert(sqe);

    io_uring_sqe_set_flags(sqe, sqe_flags);

    uintptr_t this_ptr = (uintptr_t)this;
    if (sqe_flags & IOSQE_IO_LINK || sqe_flags & IOSQE_IO_HARDLINK)
        this_ptr |= EVENT_IGNORE_FLAG;
    io_uring_sqe_set_data(sqe, (void*)this_ptr);
}

bool event_accept_submit(Event* this, struct io_uring* uring, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len) {
    assert(this);
    assert(uring);
    assert(out_client_addr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_ACCEPT;

    io_uring_prep_accept(sqe, socket, (struct sockaddr*)out_client_addr,
                         inout_addr_len, 0);
    event_sqe_fill(this, sqe, 0);

    return true;
}

bool event_send_submit(Event* this, struct io_uring* uring, fd socket,
                       CString data, uint8_t sqe_flags) {
    assert(this);
    assert(uring);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_SEND;

    io_uring_prep_send(sqe, socket, data.ptr, data.len, 0);
    event_sqe_fill(this, sqe, sqe_flags);

    return true;
}

bool event_recv_submit(Event* this, struct io_uring* uring, fd socket,
                       String data) {
    assert(this);
    assert(uring);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_RECV;

    io_uring_prep_recv(sqe, socket, data.ptr, data.len, 0);
    event_sqe_fill(this, sqe, 0);

    return true;
}

bool event_read_submit(Event* this, struct io_uring* uring, fd file,
                       String out_data, size_t nbytes, off_t offset,
                       uint8_t sqe_flags) {
    assert(this);
    assert(uring);
    assert(file >= 0);
    assert(out_data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    io_uring_prep_read(sqe, file, out_data.ptr, MIN(out_data.len, nbytes),
                       offset);
    event_sqe_fill(this, sqe, sqe_flags);

    return true;
}

bool event_close_submit(Event* this, struct io_uring* uring, fd socket) {
    assert(this);
    assert(uring);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_CLOSE;

    io_uring_prep_close(sqe, socket);
    event_sqe_fill(this, sqe, 0);

    return true;
}

bool event_timeout_submit(Event* this, struct io_uring* uring,
                          Timespec* threshold, uint32_t timeout_flags) {
    assert(this);
    assert(uring);
    assert(threshold);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_TIMEOUT;

    io_uring_prep_timeout(sqe, threshold, 0, timeout_flags);
    event_sqe_fill(this, sqe, 0);

    return true;
}

bool event_cancel_submit(Event* this, struct io_uring* uring, Event* target,
                         uint8_t sqe_flags) {
    assert(this);
    assert(uring);
    assert(target);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = EVENT_CANCEL;

    io_uring_prep_cancel(sqe, target, 0);
    event_sqe_fill(this, sqe, sqe_flags);

    return true;
}
