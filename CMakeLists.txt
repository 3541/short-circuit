cmake_minimum_required(VERSION 3.28)
project(short-circuit VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_EXTENSIONS FALSE)

set(
    CMAKE_MODULE_PATH
    "${CMAKE_SOURCE_DIR}/cmake"
    "${CMAKE_SOURCE_DIR}/boilerplate/cmake"
    ${CMAKE_MODULE_PATH}
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-O0 HAVE_O0)
if (HAVE_O0)
    add_compile_options($<$<CONFIG:Debug>:-O0>)
endif()

add_compile_definitions($<$<CONFIG:Release>:_FORTIFY_SOURCE=3>)

include(CheckIPOSupported)
check_ipo_supported(RESULT lto)
if (lto)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
endif()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    include(CTest)
    include(FetchContent)
    include(GoogleTest)

    enable_testing()

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571 # v1.14
    )
    set(INSTALL_GTEST FALSE CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_custom_target(check COMMAND "${CMAKE_CTEST_COMMAND}" --output-on-failure)
endif()

add_subdirectory(src)
