/*
 * SHORT CIRCUIT: TRY — Convenience macro for use with error/value types.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <concepts>
#include <optional>
#include <utility>

#include "sc/lib/result.hh"

namespace sc::lib::detail {

template <typename O>
concept IsOptional = requires(O o) {
    typename O::value_type;
    { o.has_value() } -> std::same_as<bool>;
    {*o};
};

template <typename R>
concept IsResult = requires(R r) {
    typename R::Type;
    typename R::ErrType;
    { r.is_ok() } -> std::same_as<bool>;
    { r.is_err() } -> std::same_as<bool>;
    { r.assume_ok() } -> std::same_as<typename R::Type>;
    { r.assume_err() } -> std::same_as<typename R::ErrType>;
};

template <typename T>
auto error_variant(std::optional<T> const&) {
    return std::nullopt;
}

template <IsResult R>
auto error_variant(R&& result) {
    return std::forward<R>(result).assume_err();
}

template <IsOptional O>
auto ok_variant(O&& optional) {
    return *std::forward<O>(optional);
}

template <IsResult R>
auto ok_variant(R&& result) {
    return std::forward<R>(result).assume_ok();
}

} // namespace sc::lib::detail

#define SC_TRY(E)                                                                                  \
    ({                                                                                             \
        auto&& _tmp = (E);                                                                         \
        if (!_tmp)                                                                                 \
            return ::sc::lib::Err{                                                                 \
                ::sc::lib::detail::error_variant(std::forward<decltype(_tmp)>(_tmp))};             \
        ::sc::lib::detail::ok_variant(std::forward<decltype(_tmp)>(_tmp));                         \
    })

#define SC_TRY_ERRNO(E) SC_TRY((::sc::lib::Result<decltype(E), ::std::error_code>::from_errno(E)))
