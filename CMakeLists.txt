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

set(SC_SRC
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

set(SC_LIB
  a3
  a3hash
  PkgConfig::liburing
)

add_executable(sc src/main.c ${SC_SRC})
target_include_directories(sc PRIVATE src)
target_link_libraries(sc ${SC_LIB})

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
  SC_VERSION="${PROJECT_VERSION}"
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

# Testing

include(CTest)
find_package(GTest)

if (GTest_FOUND)
  set(GTEST_LIB GTest::gtest_main)
else()
  set(INSTALL_GTEST FALSE CACHE BOOL "Don't install GTest.")
  add_subdirectory(extern/googletest EXCLUDE_FROM_ALL)
  set(GTEST_LIB gtest_main)

  if (WIN32 AND NOT MSVC)
    target_add_warning(gtest "no-language-extension-token" FALSE)
  endif()
endif()

add_library(sc_lib ${SC_SRC})
target_include_directories(sc_lib PUBLIC src)
target_link_libraries(sc_lib PUBLIC ${SC_LIB})
target_disable_exceptions(sc_lib)
target_disable_rtti(sc_lib)
target_add_warning_cxx(sc_lib "no-c99-designator" FALSE)
target_add_sanitizers(sc_lib TRUE)
set_target_properties(sc_lib PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS FALSE
  C_STANDARD_REQUIRED TRUE
  CXX_STANDARD 11
  CXX_EXTENSIONS FALSE
  CXX_STANDARD_REQUIRED TRUE
  C_VISIBILITY_PRESET default
  CXX_VISIBILITY_PRESET default
)
target_compile_definitions(sc_lib PRIVATE
  typeof=__typeof__
  _XOPEN_SOURCE=700
)

add_executable(sc_test
  test/uri.cc
)
target_disable_exceptions(sc_test)
target_disable_rtti(sc_test)
set_target_properties(sc_test PROPERTIES
  C_EXTENSIONS FALSE
  CXX_EXTENSIONS FALSE
)
target_add_sanitizers(sc_test TRUE)
target_add_extra_warnings(sc_test FALSE)
target_link_libraries(sc_test PRIVATE sc_lib)
target_link_libraries(sc_test PRIVATE ${GTEST_LIB})
gtest_discover_tests(sc_test)

add_dependencies(sc_test a3_test)
