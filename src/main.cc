/*
 * SHORT CIRCUIT -- A high-performance HTTP server for Linux, built on io_uring.
 *
 * Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>
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

#include <charconv>
#include <filesystem>

#include <fcntl.h>
#include <getopt.h>

#include <a3/log.h>
#include <a3/util.h>

#include "config.hh"
#include "event/future.hh"
#include "event/loop.hh"
#include "options.hh"

namespace fs = std::filesystem;

using namespace sc;

namespace sc {
Options const CONFIG { .web_root = config::DEFAULT_WEB_ROOT,
#ifdef NDEBUG
                       .log_level = A3_LOG_WARN,
#else
                       .log_level = A3_LOG_TRACE,
#endif
                       .listen_port = config::DEFAULT_LISTEN_PORT };
} // namespace sc

static void webroot_check_exists(fs::path const& root) {
    if (!fs::exists(root))
        A3_PANIC_FMT("Web root %s is inaccessible.", root.c_str());
    if (!fs::is_directory(root))
        A3_PANIC_FMT("Web root %s is not a directory.", root.c_str());
}

static void usage() {
    fprintf(stderr, "USAGE:\n\n"
                    "sc [options] [web root]\n"
                    "Options:\n"
                    "\t-h, --help\t\tShow this message and exit.\n"
                    "\t-p, --port <PORT>\tSpecify the port to listen on. (Default is 8000).\n"
                    "\t-q, --quiet\t\tBe quieter (more 'q's for more silence).\n"
                    "\t-v, --verbose\t\tPrint verbose output (more 'v's for even more "
                    "output).\n"
                    "\t    --version\t\tPrint version information.\n");
    exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
}

static void version() {
    printf("Short Circuit (sc) %s\n"
           "Copyright (c) 2020-2022, Alex O'Brien <3541ax@gmail.com>\n\n"
           "This program is free software: you can redistribute it and/or modify\n"
           "it under the terms of the GNU Affero General Public License as "
           "published\n"
           "by the Free Software Foundation, either version 3 of the License, or\n"
           "(at your option) any later version.\n\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU Affero General Public License for more details.\n\n"
           "You should have received a copy of the GNU Affero General Public "
           "License\n"
           "along with this program.  If not, see "
           "<https://www.gnu.org/licenses/>.\n",
           SC_VERSION);
    exit(EXIT_SUCCESS); // NOLINT(concurrency-mt-unsafe)
}

static void config_parse(int argc, char** argv) {
    enum { OPT_HELP, OPT_PORT, OPT_QUIET, OPT_VERBOSE, OPT_VERSION, OPT_COUNT };

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays)
    static option options[] {
        [OPT_HELP]    = { "help", no_argument, nullptr, 'h' },
        [OPT_PORT]    = { "port", required_argument, nullptr, 'p' },
        [OPT_QUIET]   = { "quiet", no_argument, nullptr, 'q' },
        [OPT_VERBOSE] = { "verbose", no_argument, nullptr, 'v' },
        [OPT_VERSION] = { "version", no_argument, nullptr, '\0' },
        [OPT_COUNT]   = { nullptr, 0, nullptr, 0 },
    };

    auto& config = const_cast<Options&>(CONFIG); // NOLINT(cppcoreguidelines-pro-type-const-cast)

    int opt       = 0;
    int longindex = 0;
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((opt = getopt_long(argc, argv, "hqvp:", options, &longindex)) != -1) {
        switch (opt) {
        case 'h':
            usage();
            break;
        case 'p': {
            in_port_t port_num = 0;
            std::from_chars(optarg, optarg + strlen(optarg), port_num);
            if (port_num > std::numeric_limits<in_port_t>::max())
                A3_PANIC_FMT("Invalid port %llu.", port_num);

            config.listen_port = static_cast<in_port_t>(port_num);
            break;
        }
        case 'q':
        case 'v': {
            auto n = config.log_level;
            if (opt == 'v' && CONFIG.log_level < A3_LOG_ERROR)
                n++;
            else if (opt == 'q' && CONFIG.log_level > A3_LOG_TRACE)
                n--;
            config.log_level = n;
            break;
        }
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
            A3_ERROR("Too many parameters.");
            usage();
        }

        config.web_root = argv[optind];
    }

    config.web_root = fs::canonical(CONFIG.web_root);
}

int main(int argc, char* argv[]) {
    a3_log_init(stderr, CONFIG.log_level);

    a3_log_init(stderr, CONFIG.log_level);
    config_parse(argc, argv);
    // Re-initialize with the potentially changed log level.
    a3_log_init(stderr, CONFIG.log_level);

    webroot_check_exists(CONFIG.web_root);

    fs::path file { "build.ninja" };
    int      fd = open(file.c_str(), O_RDONLY);
    A3_UNWRAPSD(fd);

    ev::EventLoop loop;

    /*    loop.run([](ev::EventLoop& loop, int fd) -> ev::Future<void> {
            auto buffer = std::make_unique<char[]>(512);

            auto s = co_await loop.read(fd, std::as_writable_bytes(std::span { buffer.get(), 511
       })); buffer[511] = '\0'; printf("Read %zu bytes.\n", s.unwrap()); printf("%s\n",
       buffer.get());
            }(loop, fd));*/

    return EXIT_SUCCESS;
}
