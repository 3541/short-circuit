#include <liburing.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "log.h"
#include "socket.h"
#include "util.h"

CString WEB_ROOT;

int main(void) {
    int port = DEFAULT_LISTEN_PORT;

    WEB_ROOT                   = string_from(realpath(DEFAULT_WEB_ROOT, NULL));

    log_init(stdout);

    fd              listen_socket = socket_listen(port);
    struct io_uring uring         = event_init();

    struct Connection* current;
    UNWRAPN(current, connection_accept_submit(&uring, PLAIN, listen_socket));

    log_msg(TRACE, "Entering event loop.");

    bool cont = true;
    while (cont) {
        struct io_uring_cqe* cqe;
        UNWRAPSD(io_uring_wait_cqe(&uring, &cqe));

        uintptr_t event_ptr = cqe->user_data;
        if (event_ptr & EVENT_PTR_IGNORE)
            goto next;
        struct Event* event = (struct Event*)event_ptr;

        if (cqe->res < 0) {
            log_error(-cqe->res, event_type_name(event->type).ptr);
            // Can't do much if ACCEPT doesn't work.
            if (event->type == ACCEPT) {
                free(event);
                cont = false;
            }
            goto next;
        }

        cont = connection_event_dispatch((struct Connection*)event, cqe, &uring,
                                         listen_socket);

    next:
        io_uring_cqe_seen(&uring, cqe);
    }

    return EXIT_SUCCESS;
}
