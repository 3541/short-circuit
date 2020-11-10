#include <liburing.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "http_connection.h"
#include "log.h"
#include "socket.h"
#include "util.h"

CString WEB_ROOT;

static bool cont = true;

static void sigint_handle(int no) {
    (void)no;
    cont = false;
}

static void check_webroot_exists(const char* root) {
    struct stat s;

    if (stat(root, &s) < 0)
        PANIC_FMT("Web root %s is inaccessible.", root);
    if (!S_ISDIR(s.st_mode))
        PANIC_FMT("Web root %s is not a directory.", root);
}

int main(void) {
    int port = DEFAULT_LISTEN_PORT;

    log_init(stdout);

    check_webroot_exists(DEFAULT_WEB_ROOT);
    WEB_ROOT = cstring_from(realpath(DEFAULT_WEB_ROOT, NULL));

    fd              listen_socket = socket_listen(port);
    struct io_uring uring         = event_init();

    Connection* current;
    UNWRAPN(current, connection_accept_submit(&uring, PLAIN, listen_socket));
    UNWRAPND(io_uring_submit(&uring));

    UNWRAPND(signal(SIGINT, sigint_handle) != SIG_ERR);

    log_msg(TRACE, "Entering event loop.");

    while (cont) {
        struct io_uring_cqe* cqe;
        int                  rc;
        if ((rc = io_uring_wait_cqe(&uring, &cqe)) < 0) {
            log_error(-rc, "Breaking event loop.");
            break;
        }

        for (; cqe && io_uring_sq_ready(&uring) <= URING_SUBMISSION_THRESHOLD;
             io_uring_peek_cqe(&uring, &cqe)) {
            uintptr_t event_ptr = cqe->user_data;
            if (event_ptr & EVENT_PTR_IGNORE)
                goto next;
            Event* event = (Event*)event_ptr;

            cont = connection_event_dispatch((Connection*)event, cqe, &uring,
                                             listen_socket);
        next:
            io_uring_cqe_seen(&uring, cqe);
        }

        if (io_uring_sq_ready(&uring) > 0) {
            int ev = io_uring_submit(&uring);
            log_fmt(TRACE, "Submitted %d events.", ev);
        }
    }

    http_connection_freelist_clear();

    return EXIT_SUCCESS;
}
