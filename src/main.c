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

void usage(const char* name) {
    printf("Usage:\n\t%s [:port]\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    int port = DEFAULT_LISTEN_PORT;
    if (argc > 1) {
        if (argv[1][0] != ':')
            usage(argv[0]);

        port = atoi(&argv[1][1]);
    }

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

        struct Event* event = io_uring_cqe_get_data(cqe);

        if (cqe->res < 0) {
            log_error(-cqe->res, event_type_name(event->type));
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
