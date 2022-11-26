/*
 * SHORT CIRCUIT: RESULT — Either a result, or an error.
 *
 * Copyright (c) 2022, Alex O'Brien <3541@3541.website>
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of
 * the MPL was not distributed with this file, you can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <cerrno>
#include <concepts>
#include <new>
#include <optional>
#include <string>
#include <system_error>
#include <type_traits>

namespace sc::lib {

template <typename T, typename E>
struct Result;

namespace detail {

template <typename T, typename E>
struct ResultNonVoid {
public:
    Result<T&, E&>             as_ref() requires(!std::is_void_v<T>);
    Result<T const&, E const&> as_ref() const requires(!std::is_void_v<T>);
};

template <typename E>
struct ResultNonVoid<void, E> {};

} // namespace detail

template <typename T>
struct Ok {
    T m_value;
};

template <>
struct Ok<void> {};

template <typename T>
Ok(T&&) -> Ok<T>;

template <typename T>
concept HasMessage = requires(T t) {
    { t.message() } -> std::convertible_to<std::string>;
};

template <typename E>
struct Err {
    E m_value;

    std::string message();
    std::string message() requires HasMessage<E>;
};

template <typename E>
Err(E&&) -> Err<E>;

template <typename E>
std::string Err<E>::message() {
    return "Error";
}

template <typename E>
std::string Err<E>::message() requires HasMessage<E> {
    return m_value.message();
}

template <typename T, typename E>
struct Result : public detail::ResultNonVoid<T, E> {
private:
    bool m_is_ok;
    union {
        Ok<T>  m_ok;
        Err<E> m_err;
    };

public:
    using Type    = T;
    using ErrType = E;

    template <typename U>
    // clang-format off
    requires std::constructible_from<T, U> && (!std::same_as<std::decay_t<U>, Result>)
    // NOLINTNEXTLINE(bugprone-forwarding-reference-overload)
    constexpr explicit Result(U&& value) : m_is_ok{true}, m_ok{std::forward<U>(value)} {}
    // clang-format on

    template <std::convertible_to<T> U>
    constexpr Result(Ok<U>);
    template <std::convertible_to<E> F>
    constexpr Result(Err<F>);

    constexpr Result() requires std::is_void_v<T>;

    template <std::signed_integral U>
    constexpr static Result
        from_errno(U) requires std::integral<T> && std::same_as<E, std::error_code>;

    constexpr Result(Result const&);
    constexpr Result& operator=(Result const&);

    constexpr Result(Result&&) noexcept;
    constexpr Result& operator=(Result&&) noexcept;

    constexpr bool is_ok() const;
    constexpr bool is_err() const;
    constexpr      operator bool() const;

    constexpr T assume_ok() const& requires std::is_trivially_copyable_v<T>;
    constexpr T assume_ok() &&;

    constexpr std::optional<T> ok() const& requires std::is_trivially_copyable_v<T>;
    constexpr std::optional<T> ok() &&;

    constexpr E assume_err() const& requires std::is_trivially_copyable_v<E>;
    constexpr E assume_err() &&;

    constexpr std::optional<E> err() const& requires std::is_trivially_copyable_v<E>;
    constexpr std::optional<E> err() &&;
};

template <typename T, typename E>
template <std::signed_integral U>
constexpr Result<T, E>
Result<T, E>::from_errno(U value) requires std::integral<T> && std::same_as<E, std::error_code> {
    if (value < U{0}) {
        return Err{std::make_error_code(std::errc{errno})};
    }

    return Ok{T{value}};
}

template <typename T, typename E>
template <std::convertible_to<T> U>
constexpr Result<T, E>::Result(Ok<U> ok) : m_is_ok{true}, m_ok{std::move(ok).m_value} {}

template <typename T, typename E>
template <std::convertible_to<E> F>
constexpr Result<T, E>::Result(Err<F> err) : m_is_ok{false}, m_err{std::move(err).m_value} {}

template <typename T, typename E>
constexpr Result<T, E>::Result() requires std::is_void_v<T> : m_is_ok{true} {}

template <typename T, typename E>
constexpr Result<T, E>::Result(Result const& other) : m_is_ok{other.m_is_ok} {
    if (m_is_ok) {
        new (&m_ok) Ok<T>{other.m_ok.m_value};
    } else {
        new (&m_err) Err<E>{other.m_err.m_value};
    }
}

template <typename T, typename E>
constexpr Result<T, E>& Result<T, E>::operator=(Result const& other) {
    if (&other == this)
        return *this;

    new (this) Result{other};
    return *this;
}

template <typename T, typename E>
constexpr Result<T, E>::Result(Result&& other) noexcept : m_is_ok{other.m_is_ok} {
    if (m_is_ok) {
        new (&m_ok) Ok<T>{std::move(other).m_ok.m_value};
    } else {
        new (&m_err) Err<E>{std::move(other).m_err.m_value};
    }
}

template <typename T, typename E>
constexpr Result<T, E>& Result<T, E>::operator=(Result&& other) noexcept {
    if (&other == this)
        return *this;

    new (this) Result{std::move(other)};
    return *this;
}

template <typename T, typename E>
constexpr bool Result<T, E>::is_ok() const {
    return m_is_ok;
}

template <typename T, typename E>
constexpr bool Result<T, E>::is_err() const {
    return !m_is_ok;
}

template <typename T, typename E>
constexpr Result<T, E>::operator bool() const {
    return is_ok();
}

template <typename T, typename E>
constexpr T Result<T, E>::assume_ok() const& requires std::is_trivially_copyable_v<T> {
    return m_ok.m_value;
}

template <typename T, typename E>
constexpr T Result<T, E>::assume_ok() && {
    return std::move(m_ok).m_value;
}

template <typename T, typename E>
constexpr std::optional<T> Result<T, E>::ok() const& requires std::is_trivially_copyable_v<T> {
    return is_ok() ? assume_ok() : std::nullopt;
}

template <typename T, typename E>
constexpr std::optional<T> Result<T, E>::ok() && {
    return is_ok() ? std::move(*this).assume_ok() : std::nullopt;
}

template <typename T, typename E>
constexpr E Result<T, E>::assume_err() const& requires std::is_trivially_copyable_v<E> {
    return m_err.m_value;
}

template <typename T, typename E>
constexpr E Result<T, E>::assume_err() && {
    return std::move(m_err).m_value;
}

template <typename T, typename E>
constexpr std::optional<E> Result<T, E>::err() const& requires std::is_trivially_copyable_v<E> {
    return is_err() ? assume_err() : std::nullopt;
}

template <typename T, typename E>
constexpr std::optional<E> Result<T, E>::err() && {
    return is_err() ? std::move(*this).assume_err() : std::nullopt;
}

} // namespace sc::lib
