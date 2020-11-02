#include "event.h"

#include <assert.h>
#include <liburing.h>
#include <stdbool.h>
#include <string.h>
#include <sys/utsname.h>

#include "config.h"
#include "util.h"

const char* event_type_name(enum EventType ty) {
#define _EVENT_TYPE(E) { E, #E },
    static const struct {
        enum EventType ty;
        const char*    name;
    } EVENT_NAMES[] = { EVENT_TYPE_ENUM{ 0, NULL } };
#undef _EVENT_TYPE

    for (size_t i = 0; i < sizeof(EVENT_NAMES) / sizeof(EVENT_NAMES[0]); i++) {
        if (ty == EVENT_NAMES[i].ty)
            return EVENT_NAMES[i].name;
    }

    return "INVALID";
}

#define REQUIRE_OP(P, OP)                                                      \
    do {                                                                       \
        if (!io_uring_opcode_supported(P, OP))                                 \
            PANIC_FMT(                                                         \
                "Required io_uring op %s is not supported by the kernel.",     \
                #OP);                                                          \
    } while (0)

// Check that the kernel is recent enough to support io_uring and
// io_uring_probe.
static void event_check_kver() {
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

// All ops used should be checked here.
static void event_check_ops() {
    struct io_uring_probe* probe = io_uring_get_probe();

    REQUIRE_OP(probe, IORING_OP_ACCEPT);
    REQUIRE_OP(probe, IORING_OP_RECV);
    REQUIRE_OP(probe, IORING_OP_CLOSE);

    free(probe);
}

struct io_uring event_init() {
    event_check_kver();

    struct io_uring ret;
    UNWRAPSD(io_uring_queue_init(URING_ENTRIES, &ret, 0));

    event_check_ops();

    return ret;
}

bool event_accept_submit(struct Event* this, struct io_uring* uring, fd socket,
                         struct sockaddr_in* out_client_addr,
                         socklen_t*          inout_addr_len) {
    assert(this);
    assert(uring);
    assert(out_client_addr);

    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    if (!sqe)
        return false;
    this->type = ACCEPT;

    io_uring_prep_accept(sqe, socket, (struct sockaddr*)out_client_addr,
                         inout_addr_len, 0);
    io_uring_sqe_set_data(sqe, this);

    return io_uring_submit(uring);
}

bool event_send_submit(struct Event* this, struct io_uring* uring, fd socket,
                       const void* buf, size_t len, int flags) {
    assert(this);
    assert(uring);
    assert(buf);

    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    TRYB(sqe);
    this->type = SEND;

    uintptr_t this_ptr = (uintptr_t)this;

    if (flags & IOSQE_IO_LINK) {
        this_ptr |= EVENT_PTR_IGNORE;
    }

    io_uring_prep_send(sqe, socket, buf, len, flags);
    io_uring_sqe_set_data(sqe, (void*)this_ptr);

    return io_uring_submit(uring);
}

bool event_recv_submit(struct Event* this, struct io_uring* uring, fd socket,
                       void* out_buf, size_t buf_len) {
    assert(this);
    assert(uring);
    assert(out_buf);

    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    if (!sqe)
        return false;
    this->type = RECV;

    io_uring_prep_recv(sqe, socket, out_buf, buf_len, 0);
    io_uring_sqe_set_data(sqe, this);

    return io_uring_submit(uring);
}

bool event_close_submit(struct Event* this, struct io_uring* uring, fd socket) {
    assert(this);
    assert(uring);

    struct io_uring_sqe* sqe = io_uring_get_sqe(uring);
    if (!sqe)
        return false;
    this->type = CLOSE;

    io_uring_prep_close(sqe, socket);
    io_uring_sqe_set_data(sqe, this);

    return io_uring_submit(uring);
}
