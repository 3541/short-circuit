/*
 * SHORT CIRCUIT: TIMEOUT -- Timeouts for IO events.
 *
 * Copyright (c) 2022, Alex O'Brien <3541ax@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU Affero General Public License as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <time.h>

#include <a3/cpp.h>
#include <a3/ll.h>
#include <a3/types.h>

A3_H_BEGIN

typedef struct ScTimer ScTimer;

typedef struct ScTimeout ScTimeout;
typedef void (*ScTimeoutCb)(ScTimeout*);

typedef struct ScTimeout {
    struct timespec deadline;
    A3_LL_LINK(ScTimeout) link;
    time_t      delay_s;
    ScTimeoutCb done;
} ScTimeout;

A3_EXPORT ScTimer*               sc_timer_new(void);
A3_EXPORT void                   sc_timer_free(ScTimer*);
A3_EXPORT struct timespec const* sc_timer_next(ScTimer const*);
A3_EXPORT void                   sc_timer_tick(ScTimer*);

A3_EXPORT void sc_timeout_init(ScTimeout*, ScTimeoutCb, time_t delay_s);
A3_EXPORT void sc_timeout_add(ScTimer*, ScTimeout*);
A3_EXPORT void sc_timeout_reset(ScTimeout*);
A3_EXPORT void sc_timeout_cancel(ScTimeout*);

A3_H_END
