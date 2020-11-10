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

#include "config.h"
#include "log.h"
#include "ptr.h"
#include "ptr_util.h"
#include "socket.h"
#include "util.h"

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
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_CLOSE);

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

bool event_accept_submit(Event* this, struct io_uring* uring, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len) {
    assert(this);
    assert(uring);
    assert(out_client_addr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = ACCEPT;

    io_uring_prep_accept(sqe, socket, (struct sockaddr*)out_client_addr,
                         inout_addr_len, 0);
    io_uring_sqe_set_data(sqe, this);

    return true;
}

bool event_send_submit(Event* this, struct io_uring* uring, fd socket,
                       CByteString data, unsigned sqe_flags) {
    assert(this);
    assert(uring);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = SEND;

    uintptr_t this_ptr = (uintptr_t)this;
    if (sqe_flags & IOSQE_IO_LINK)
        this_ptr |= EVENT_PTR_IGNORE;

    io_uring_prep_send(sqe, socket, data.ptr, data.len, 0);
    io_uring_sqe_set_flags(sqe, sqe_flags);
    io_uring_sqe_set_data(sqe, (void*)this_ptr);

    return true;
}

bool event_recv_submit(Event* this, struct io_uring* uring, fd socket,
                       ByteString data) {
    assert(this);
    assert(uring);
    assert(data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);
    this->type = RECV;

    io_uring_prep_recv(sqe, socket, data.ptr, data.len, 0);
    io_uring_sqe_set_data(sqe, this);

    return true;
}

bool event_read_submit(Event* this, struct io_uring* uring, fd file,
                       ByteString out_data, size_t nbytes, off_t offset,
                       unsigned sqe_flags) {
    assert(this);
    assert(uring);
    assert(file >= 0);
    assert(out_data.ptr);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    TRYB(sqe);

    uintptr_t this_ptr = (uintptr_t)this;
    if (sqe_flags & IOSQE_IO_LINK)
        this_ptr |= EVENT_PTR_IGNORE;

    io_uring_prep_read(sqe, file, out_data.ptr, MIN(out_data.len, nbytes),
                       offset);
    io_uring_sqe_set_flags(sqe, sqe_flags);
    io_uring_sqe_set_data(sqe, (void*)this_ptr);

    return true;
}

bool event_close_submit(Event* this, struct io_uring* uring, fd socket) {
    assert(this);
    assert(uring);

    struct io_uring_sqe* sqe = event_get_sqe(uring);
    if (!sqe)
        return false;
    this->type = CLOSE;

    io_uring_prep_close(sqe, socket);
    io_uring_sqe_set_data(sqe, this);

    return true;
}
