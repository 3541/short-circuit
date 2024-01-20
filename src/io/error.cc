/*
 * ERROR -- Common IO errors.
 *
 * Copyright (c) 2023-2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

module;

#include <cerrno>
#include <ostream>
#include <system_error>

module sc.io.error;

namespace sc::io {

RequestFailed::RequestFailed(std::error_code error) : m_reason{error} {}

RequestFailed RequestFailed::from_errno() noexcept {
    return RequestFailed{std::error_code{errno, std::system_category()}};
}

std::ostream& operator<<(std::ostream& stream, RequestFailed const& error) {
    return stream << "Request failed: " << error.m_reason;
}

} // namespace sc::io
