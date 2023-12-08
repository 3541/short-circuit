/*
 * INTERFACE -- Platform-agnostic IO interface.
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module sc.io.interface;

namespace sc::io {

Interface& Interface::the() noexcept {
    static thread_local Interface INSTANCE;
    return INSTANCE;
}

} // namespace sc::io
