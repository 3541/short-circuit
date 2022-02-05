#pragma once

#include <version>

#ifdef __cpp_lib_to_underlying
#define A3_TU_STD
#include <utility>
#else
#include <type_traits>
#endif

namespace a3 {
#ifdef A3_TU_STD
template <typename E>
to_underlying = std::to_underlying<E>;
#else
template <typename E>
constexpr auto to_underlying(E e) {
    return static_cast<std::underlying_type_t<E>>(e);
}
#endif
} // namespace a3

#undef A3_TU_STD
