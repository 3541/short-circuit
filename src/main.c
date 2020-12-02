#include <liburing.h>
#include <liburing/io_uring.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <a3/log.h>
#include <a3/util.h>

#include "config.h"
#include "connection.h"
#include "event.h"
#include "forward.h"
#include "http/connection.h"
#include "listen.h"
#include "ptr.h"
#include "timeout.h"

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

int main(int argc, char** argv) {
    (void)argv;

#if defined(DEBUG_BUILD) && !defined(PROFILE)
    log_init(stdout, TRACE);
#else
    log_init(stdout, WARN);
#endif

    check_webroot_exists(DEFAULT_WEB_ROOT);
    WEB_ROOT = CS_OF(realpath(DEFAULT_WEB_ROOT, NULL));

    struct io_uring uring = event_init();

    Listener* listeners   = NULL;
    size_t    n_listeners = 0;
    if (argc < 2) {
        n_listeners = 1;
        UNWRAPN(listeners, calloc(1, sizeof(Listener)));
        listener_init(&listeners[0], DEFAULT_LISTEN_PORT, PLAIN);
    } else {
        PANIC("TODO: Parse arguments.");
    }
    listener_accept_all(listeners, n_listeners, &uring);
    UNWRAPND(io_uring_submit(&uring));

    connection_timeout_init();

    UNWRAPND(signal(SIGINT, sigint_handle) != SIG_ERR);
    log_msg(TRACE, "Entering event loop.");

#ifdef PROFILE
    time_t init_time = time(NULL);
#endif

    while (cont) {
        struct io_uring_cqe* cqe;
        int                  rc;
#ifdef PROFILE
        struct __kernel_timespec timeout = { .tv_sec = 1, .tv_nsec = 0 };
        if (((rc = io_uring_wait_cqe_timeout(&uring, &cqe, &timeout)) < 0 &&
             rc != -ETIME) ||
            time(NULL) > init_time + 20) {
            if (rc < 0)
                log_error(-rc, "Breaking event loop.");
            break;
        }
#else
        if ((rc = io_uring_wait_cqe(&uring, &cqe)) < 0 && rc != -ETIME) {
            log_error(-rc, "Breaking event loop.");
            break;
        }
#endif

        for (; cqe && io_uring_sq_ready(&uring) <= URING_SUBMISSION_THRESHOLD;
             io_uring_peek_cqe(&uring, &cqe)) {
            uintptr_t event_ptr = cqe->user_data;
            if (event_ptr & EVENT_IGNORE_FLAG)
                goto next;
            Event* event = (Event*)event_ptr;

            if (event->type == TIMEOUT)
                cont = timeout_handle((TimeoutQueue*)event, &uring, cqe);
            else
                cont =
                    connection_event_dispatch((Connection*)event, &uring, cqe);
        next:
            io_uring_cqe_seen(&uring, cqe);
        }

        listener_accept_all(listeners, n_listeners, &uring);

        if (io_uring_sq_ready(&uring) > 0) {
            int ev = io_uring_submit(&uring);
            log_fmt(TRACE, "Submitted %d event(s).", ev);
        }
    }

    http_connection_freelist_clear();
    free(listeners);

    return EXIT_SUCCESS;
}
