/*
 * TRY -- Imitation of the Rust try!() macro, for use with std::expected.
 *
 * Copyright (c) 2022, 2024, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <expected>
#include <ostream>
#include <source_location>
#include <string>

#include "sc/lib/fwd.hh"

import sc.lib.stream;

namespace sc::lib {

template <typename T>
struct WithContext {
private:
    T                    m_error;
    std::source_location m_loc;
    std::string          m_context;

public:
    WithContext(T error, std::string context,
                std::source_location loc = std::source_location::current());

    friend std::ostream& operator<<(std::ostream& stream, WithContext const& error) {
        return stream << "[" << error.m_loc.function_name() << " @ " << error.m_loc.file_name()
                      << " " << error.m_loc.line() << ":" << error.m_loc.column() << "] "
                      << error.m_context << ": " << error.m_error;
    }

    friend auto stream_view(Stream<WithContext<T> const&> error) {
        return Stream{WithContext<decltype(stream_view(error.m_inner.m_error))>{
            stream_view(error.m_inner.m_error), error.m_inner.m_context, error.m_inner.m_loc}};
    }
};

template <typename T>
WithContext<T>::WithContext(T error, std::string context, std::source_location loc) :
    m_error{std::move(error)}, m_loc{loc}, m_context{std::move(context)} {}

template <typename T>
WithContext<T> with_context(T&& error, std::string context) {
    return WithContext<T>{SC_FWD(error), std::move(context)};
}

template <typename T>
T with_context(T&& error) {
    return SC_FWD(error);
}

} // namespace sc::lib

#define SC_TRY_IMPL(E, RET, ...)                                        \
    ({                                                                                             \
        auto&& _tmp = (E);                                                                         \
        if (!_tmp)                                                                                 \
            RET std::unexpected{                                                                \
                ::sc::lib::with_context(SC_FWD(_tmp).error() __VA_OPT__(, ) __VA_ARGS__)};         \
        SC_FWD(_tmp).value();                                                                      \
    })

#define SC_TRY(E, ...) SC_TRY_IMPL(E, return, __VA_ARGS__)
#define SC_CO_TRY(E, ...) SC_TRY_IMPL(E, co_return, __VA_ARGS__)
