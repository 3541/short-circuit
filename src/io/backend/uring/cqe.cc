/*
 * SHORT CIRCUIT: SQE — Submitted, awaitable io_uring operation.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sc/io/backend/uring/cqe.hh>
#include <sc/lib/from.hh>

namespace sc::io::backend::uring {

void Cqe::resume(lib::From<Uring>, std::int32_t result) {
    auto handle = m_handle;
    m_result    = result;
    handle.resume();
}

} // namespace sc::io::backend::uring
