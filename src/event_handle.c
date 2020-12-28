#include "event_handle.h"

#include <assert.h>
#include <liburing.h>
#include <liburing/io_uring.h>

#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event_internal.h"
#include "timeout.h"

void event_queue_init(EventQueue* queue) {
    assert(queue);

    SLL_INIT(Event)(queue);
}

void event_handle(Event* event, struct io_uring* uring) {
    assert(event);
    assert(uring);

    uintptr_t target_ptr = (uintptr_t)event->target;
    bool      chain      = target_ptr & EVENT_CHAIN;
    bool      ignore     = target_ptr & EVENT_IGNORE;

    void*     target = (void*)(target_ptr & ~(EVENT_CHAIN | EVENT_IGNORE));
    int32_t   status = event->status;
    EventType type   = event->type;

    event_free(event);

    if (ignore) {
        if (status < 0)
            log_error(-status, "ignored event failed");
        return;
    }

    switch (type) {
    case EVENT_ACCEPT:
    case EVENT_CLOSE:
    case EVENT_OPENAT:
    case EVENT_READ:
    case EVENT_RECV:
    case EVENT_SEND:
    case EVENT_SPLICE:
        connection_event_handle(target, uring, type, status, chain);
        return;
    case EVENT_TIMEOUT:
        timeout_handle(target, uring, status);
        return;
    case EVENT_CANCEL:
    case EVENT_INVALID:
        return;
    }

    UNREACHABLE();
}

// Dequeue all CQEs and handle as many as possible.
void event_handle_all(EventQueue* queue, struct io_uring* uring) {
    assert(queue);
    assert(uring);

    struct io_uring_cqe* cqe;
    for (io_uring_peek_cqe(uring, &cqe); cqe; io_uring_peek_cqe(uring, &cqe)) {
        Event* event  = io_uring_cqe_get_data(cqe);
        int    status = cqe->res;

        // Remove from the CQ.
        io_uring_cqe_seen(uring, cqe);

        if (!event)
            continue;
        event->status = status;

        EventTarget* target = (EventTarget*)((uintptr_t)event->target &
                                             ~(EVENT_CHAIN | EVENT_IGNORE));

        // Remove from the in-flight list.
        SLL_REMOVE(Event)(target, event);

        // Add to the to-process queue.
        SLL_ENQUEUE(Event)(queue, event);
    }

    // Now, handle as many of the queued CQEs as possible without filling the
    // SQ.
    if (io_uring_sq_space_left(uring) <= URING_SQ_LEAVE_SPACE)
        return;
    for (Event* event = SLL_DEQUEUE(Event)(queue);
         event && io_uring_sq_space_left(uring) > URING_SQ_LEAVE_SPACE;
         event = SLL_POP(Event)(queue))
        event_handle(event, uring);

    return;
}
