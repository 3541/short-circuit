/*
 * ERROR -- Miscellaneous error handling.
 *
 * Copyright (c) 2023-2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cinttypes>
#include <source_location>
#include <string>

#include <a3/log.h>
#include <a3/util.h>

module sc.lib.error;

namespace sc::lib::error {

[[noreturn]] void must_fail(std::string const& msg, std::source_location loc) noexcept {
    A3_ERROR_F("[%s %" PRIuLEAST32 ":%" PRIuLEAST32 "] %s: %s", loc.file_name(), loc.line(),
               loc.column(), loc.function_name(), msg.data());
    A3_PANIC("must()");
}

} // namespace sc::lib::error
