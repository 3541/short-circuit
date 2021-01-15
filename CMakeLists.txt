cmake_minimum_required(VERSION 3.9...3.17)

project(
  short-circuit
  VERSION 0.1.0
  LANGUAGES C CXX
)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/boilerplate/cmake" ${CMAKE_MODULE_PATH})

set(default_build_type "Release")
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Unspecified build type. Using ${default_build_type}.")
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    "Debug" "Release" "RelWithDebInfo")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_support)
if (lto_support)
  message(STATUS "LTO support found.")
else()
  message(WARNING "LTO is not supported.")
endif()

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

find_package(PkgConfig REQUIRED)
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing)

add_subdirectory(extern/liba3 EXCLUDE_FROM_ALL)

add_executable(sc
  src/main.c
  src/event/mod.cc
  src/event/handle.cc
  src/file.c
  src/connection.c
  src/http/connection.c
  src/http/headers.c
  src/http/parse.c
  src/http/request.c
  src/http/response.c
  src/http/types.c
  src/listen.c
  src/timeout.c
  src/uri.c
)
target_include_directories(sc PRIVATE src)
target_link_libraries(sc a3 a3hash PkgConfig::liburing)

include(DisableExceptions)
include(DisableRTTI)
target_disable_exceptions(sc)
target_disable_rtti(sc)

include (ClangTidy)
include(IWYU)
target_add_iwyu(sc)
target_add_clang_tidy(sc)

include(Sanitize)
target_add_sanitizers(sc FALSE)

include(AddWarnings)
target_add_extra_warnings(sc FALSE)
target_add_warning_cxx(sc "no-c99-designator" FALSE)
set_target_properties(sc PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS FALSE
  C_STANDARD_REQUIRED TRUE
  CXX_STANDARD 11
  CXX_EXTENSIONS FALSE
  CXX_STANDARD_REQUIRED TRUE
)
target_compile_definitions(sc PRIVATE
  typeof=__typeof__ # For liburing, since it assumes GNU extensions.
  _XOPEN_SOURCE=700
)
if (lto_support)
  set_target_properties(sc PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
  )
endif()

option(PROFILE_BUILD "Build for profiling.")
if (PROFILE_BUILD)
  target_compile_definitions(sc PRIVATE PROFILE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(sc PRIVATE DEBUG_BUILD)
endif()
