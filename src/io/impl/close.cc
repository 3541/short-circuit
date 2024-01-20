/*
 * FILE CLOSE -- Wiring of generic file destructor to interface-specific close().
 *
 * Copyright (c) 2023, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#ifdef SC_IO_BACKEND_FILE_FLAGS
#include <cstdint>
#include <utility>
#endif

module sc.io.file.close;

import sc.io.interface;

namespace sc::io {

#ifdef SC_IO_BACKEND_FILE_FLAGS
void close(std::pair<unsigned, std::uint8_t> file) noexcept {
#else
void close(unsigned file) noexcept {
#endif
    Interface::the().close(file);
}

} // namespace sc::io
