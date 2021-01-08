/*
 * SHORT CIRCUIT -- A high-performance HTTP server for Linux, built on io_uring.
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

#include <errno.h>
#include <liburing.h>
#include <liburing/io_uring.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "config_runtime.h"
#include "connection.h"
#include "event.h"
#include "event/handle.h"
#include "http/connection.h"
#include "listen.h"
#include "timeout.h"

Config CONFIG = { CS_NULL,
#if defined(DEBUG_BUILD) && !defined(PROFILE)
                  TRACE
#else
                  WARN
#endif
};

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

static void usage_print_and_die(void) {
    fprintf(stderr,
            "USAGE:\n"
            "sc [options]\n"
            "Options:\n"
            "\t-h\tShow this message and exit.\n"
            "\t-v\tPrint verbose output (more 'v's for even more output).\n"
            "\t-q\tBe quieter (more 'q's for more silence).\n");
    exit(EXIT_FAILURE);
}

static void config_parse(int argc, char** argv) {
    int opt;

    while ((opt = getopt(argc, argv, "vq")) != -1) {
        switch (opt) {
        case 'v':
            if (CONFIG.log_level > TRACE)
                CONFIG.log_level--;
            break;
        case 'q':
            if (CONFIG.log_level < ERROR)
                CONFIG.log_level++;
            break;
        case 'h':
            usage_print_and_die();
            break;
        default:
            fprintf(stderr, "Unrecognized option '%c'.\n", opt);
            usage_print_and_die();
            break;
        }
    }
}

int main(int argc, char** argv) {
    (void)argv;

    config_parse(argc, argv);
    log_init(stderr, CONFIG.log_level);

    check_webroot_exists(DEFAULT_WEB_ROOT);
    CONFIG.web_root = CS_OF(realpath(DEFAULT_WEB_ROOT, NULL));
    http_connection_pool_init();
    file_cache_init();
    connection_timeout_init();

    struct io_uring uring = event_init();

    Listener* listeners   = NULL;
    size_t    n_listeners = 0;

    // TODO: Support multiple listeners.
    n_listeners = 1;
    UNWRAPN(listeners, calloc(1, sizeof(Listener)));
    listener_init(&listeners[0], DEFAULT_LISTEN_PORT, PLAIN);

    listener_accept_all(listeners, n_listeners, &uring);
    UNWRAPND(io_uring_submit(&uring));

    UNWRAPND(signal(SIGINT, sigint_handle) != SIG_ERR);
    UNWRAPND(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    log_msg(TRACE, "Entering event loop.");

#ifdef PROFILE
    time_t init_time = time(NULL);
#endif

    EventQueue queue;
    event_queue_init(&queue);
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

        event_handle_all(&queue, &uring);

        listener_accept_all(listeners, n_listeners, &uring);

        if (io_uring_sq_ready(&uring) > 0) {
            int ev = io_uring_submit(&uring);
            log_fmt(TRACE, "Submitted %d event(s).", ev);
        }
    }

    http_connection_pool_free();
    free(listeners);
    file_cache_destroy(&uring);

    return EXIT_SUCCESS;
}
