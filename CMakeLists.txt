cmake_minimum_required(VERSION 3.9...3.17)

project(
  urs
  VERSION 0.1.0
  LANGUAGES C
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

add_compile_options(-Wall -Wextra)

set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

find_package(PkgConfig REQUIRED)
pkg_check_modules(liburing REQUIRED IMPORTED_TARGET liburing)

add_subdirectory(extern/liba3 EXCLUDE_FROM_ALL)

add_executable(urs
  src/main.c
  src/event.c
  src/connection.c
  src/http/connection.c
  src/http/headers.c
  src/http/parse.c
  src/http/request.c
  src/http/response.c
  src/http/types.c
  src/listen.c
  src/socket.c
  src/timeout.c
  src/uri.c
)
target_include_directories(urs PRIVATE src)
target_link_libraries(urs a3 a3hash PkgConfig::liburing)

include(AddWarnings)
target_add_extra_warnings(urs)
set_target_properties(urs PROPERTIES
  C_STANDARD 11
  C_EXTENSIONS OFF
)
target_compile_definitions(urs PRIVATE
  typeof=__typeof__ # For liburing, since it assumes GNU extensions.
  _XOPEN_SOURCE=700
)
if (lto_support)
  set_target_properties(urs PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
    INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE
  )
endif()

if (PROFILE_BUILD)
  target_compile_definitions(urs PRIVATE PROFILE)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(urs PRIVATE DEBUG_BUILD)
endif()
