/*
 * FILE CLOSE -- Wiring of generic file destructor to interface-specific close().
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <utility>

module sc.io.file;

import sc.io.interface;

namespace sc::io {

void close(File&& file) noexcept { Interface::the().close(std::move(file)); }

} // namespace sc::io
