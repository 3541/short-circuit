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
 *
 * Note: This whole file is a bit of a hack at the moment, and should probably
 * be regarded more as a test harness for development purposes than an actual
 * final interface.
 */

#include <errno.h>
#include <getopt.h>
#include <liburing.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <a3/log.h>
#include <a3/str.h>
#include <a3/util.h>

#include "config.h"
#include "config_runtime.h"
#include "connection.h"
#include "event.h"
#include "event/handle.h"
#include "file.h"
#include "forward.h"
#include "http/connection.h"
#include "listen.h"

Config CONFIG = { .web_root    = DEFAULT_WEB_ROOT,
                  .listen_port = DEFAULT_LISTEN_PORT,
#if defined(DEBUG_BUILD) && !defined(PROFILE)
                  .log_level = LOG_TRACE
#else
                  .log_level = LOG_WARN
#endif
};

static bool cont = true;

static void sigint_handle(int no) {
    (void)no;
    cont = false;
}

static void check_webroot_exists(A3CString root) {
    struct stat s;

    if (stat(A3_S_AS_C_STR(root), &s) < 0)
        A3_PANIC_FMT("Web root %s is inaccessible.", root);
    if (!S_ISDIR(s.st_mode))
        A3_PANIC_FMT("Web root %s is not a directory.", root);
}

static void usage(void) {
    fprintf(stderr, "USAGE:\n\n"
                    "sc [options] [web root]\n"
                    "Options:\n"
                    "\t-h, --help\t\tShow this message and exit.\n"
                    "\t-p, --port <PORT>\tSpecify the port to listen on. (Default is 8000).\n"
                    "\t-q, --quiet\t\tBe quieter (more 'q's for more silence).\n"
                    "\t-v, --verbose\t\tPrint verbose output (more 'v's for even more output).\n"
                    "\t    --version\t\tPrint version information.\n");
    exit(EXIT_FAILURE);
}

static void version(void) {
    printf("Short Circuit (sc) %s\n"
           "Copyright (c) 2020-2021, Alex O'Brien <3541ax@gmail.com>\n\n"
           "This program is free software: you can redistribute it and/or modify\n"
           "it under the terms of the GNU Affero General Public License as published\n"
           "by the Free Software Foundation, either version 3 of the License, or\n"
           "(at your option) any later version.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU Affero General Public License for more details.\n\n"
           "You should have received a copy of the GNU Affero General Public License\n"
           "along with this program.  If not, see <https://www.gnu.org/licenses/>.\n",
           SC_VERSION);
    exit(EXIT_SUCCESS);
}

enum { OPT_HELP, OPT_PORT, OPT_QUIET, OPT_VERBOSE, OPT_VERSION, _OPT_COUNT };

static void config_parse(int argc, char** argv) {
    static struct option options[] = { [OPT_HELP]    = { "help", no_argument, NULL, 'h' },
                                       [OPT_PORT]    = { "port", required_argument, NULL, 'p' },
                                       [OPT_QUIET]   = { "quiet", no_argument, NULL, 'q' },
                                       [OPT_VERBOSE] = { "verbose", no_argument, NULL, 'v' },
                                       [OPT_VERSION] = { "version", no_argument, NULL, '\0' },
                                       [_OPT_COUNT]  = { 0, 0, 0, 0 } };

    int      opt;
    int      longindex;
    uint64_t port_num;
    while ((opt = getopt_long(argc, argv, "hqvp:", options, &longindex)) != -1) {
        switch (opt) {
        case 'h':
            usage();
            break;
        case 'p':
            port_num = strtoul(optarg, NULL, 10);
            if (port_num > UINT16_MAX) {
                a3_log_msg(LOG_ERROR, "Invalid port.");
                exit(EXIT_FAILURE);
            }

            CONFIG.listen_port = (in_port_t)port_num;
            break;
        case 'q':
            if (CONFIG.log_level < LOG_ERROR)
                CONFIG.log_level++;
            break;
        case 'v':
            if (CONFIG.log_level > LOG_TRACE)
                CONFIG.log_level--;
            break;
        default:
            if (opt == 0) {
                switch (longindex) {
                case OPT_VERSION:
                    version();
                    break;
                default:
                    fprintf(stderr, "Unrecognized long option.\n");
                    usage();
                    break;
                }
            } else {
                usage();
            }
            break;
        }
    }

    // Non-option parameters to parse.
    if (optind < argc) {
        if (argc - optind > 1) {
            a3_log_msg(LOG_ERROR, "Too many parameters.");
            usage();
        }

        CONFIG.web_root = A3_CS_OF(argv[optind]);
    }

    CONFIG.web_root = A3_CS_OF(realpath(A3_S_AS_C_STR(CONFIG.web_root), NULL));
}

int main(int argc, char** argv) {
    (void)argv;

    a3_log_init(stderr, CONFIG.log_level);
    config_parse(argc, argv);
    // Re-initialize with the potentially changed log level.
    a3_log_init(stderr, CONFIG.log_level);

    check_webroot_exists(CONFIG.web_root);
    http_connection_pool_init();
    file_cache_init();
    connection_timeout_init();

    struct io_uring uring = event_init();

    Listener* listeners   = NULL;
    size_t    n_listeners = 0;

    // TODO: Support multiple listeners.
    n_listeners = 1;
    A3_UNWRAPN(listeners, calloc(1, sizeof(Listener)));
    listener_init(&listeners[0], CONFIG.listen_port, TRANSPORT_PLAIN);

    listener_accept_all(listeners, n_listeners, &uring);
    A3_UNWRAPND(io_uring_submit(&uring));

    A3_UNWRAPND(signal(SIGINT, sigint_handle) != SIG_ERR);
    A3_UNWRAPND(signal(SIGPIPE, SIG_IGN) != SIG_ERR);
    a3_log_msg(LOG_TRACE, "Entering event loop.");

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
        if (((rc = io_uring_wait_cqe_timeout(&uring, &cqe, &timeout)) < 0 && rc != -ETIME) ||
            time(NULL) > init_time + 20) {
            if (rc < 0)
                a3_log_error(-rc, "Breaking event loop.");
            break;
        }
#else
        if ((rc = io_uring_wait_cqe(&uring, &cqe)) < 0 && rc != -ETIME) {
            a3_log_error(-rc, "Breaking event loop.");
            break;
        }
#endif

        event_handle_all(&queue, &uring);

        listener_accept_all(listeners, n_listeners, &uring);

        if (io_uring_sq_ready(&uring) > 0) {
            int ev = io_uring_submit(&uring);
            a3_log_fmt(LOG_TRACE, "Submitted %d event(s).", ev);
        }
    }

    http_connection_a3_pool_free();
    free(listeners);
    file_cache_destroy(&uring);

    return EXIT_SUCCESS;
}
