/*
 * SHORT CIRCUIT: EVENT LOOP (URING) — Glue to tie the generic event loop to the io_uring
 * implementation.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <sc/io/backend/uring/uring.hh>

namespace sc::io::backend {

using EventLoopImpl = uring::Uring;

}
