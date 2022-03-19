/*
 * SHORT CIRCUIT: IO -- IO event loop.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
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

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>

#include <a3/log.h>

#include <sc/coroutine.h>
#include <sc/io.h>

static volatile sig_atomic_t SC_TERMINATE = false;

static void sc_signal_handler(int signum) {
    (void)signum;

    SC_TERMINATE = true;
}

void sc_io_event_loop_run(ScEventLoop* ev) {
    assert(ev);

    if (signal(SIGINT, sc_signal_handler) == SIG_ERR) {
        A3_ERRNO(errno, "failed to register signal handler");
        abort();
    }

    while (!SC_TERMINATE && sc_co_count() > 0)
        sc_io_event_loop_pump(ev);
}
