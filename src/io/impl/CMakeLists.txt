add_library(io_impl STATIC close.cc interface.cc)
sc_target(io_impl)
target_sources(io_impl PUBLIC FILE_SET facade TYPE CXX_MODULES FILES interface.ccm)
target_link_libraries(io_impl PRIVATE co config io_lib lib)

set(supported_backends "io_uring" "poll")

find_library(liburing uring)
if (liburing)
    list(APPEND backends "io_uring")
endif()

include(CheckCXXSymbolExists)
check_cxx_symbol_exists("poll" "poll.h" HAVE_POLL)

if (HAVE_POLL)
    list(APPEND backends "poll")
endif()

if (NOT backends)
    message(FATAL_ERROR "No backend found")
endif()

set(SC_BACKEND "default" CACHE STRING "Override backend selection")
if (NOT SC_BACKEND STREQUAL "default")
    if (NOT SC_BACKEND IN_LIST supported_backends)
        message(FATAL_ERROR "Backend ${SC_BACKEND} not supported. Valid backends: ${supported_backends}.")
    endif()

    if (NOT SC_BACKEND IN_LIST backends)
        message(FATAL_ERROR "Backend ${SC_BACKEND} not available.")
    endif()

    set(backend ${SC_BACKEND})
else()
    list(POP_FRONT backends backend)
endif()

message(STATUS "Using backend: ${backend}.")

add_subdirectory("${backend}")
