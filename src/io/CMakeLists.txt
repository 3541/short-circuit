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

list(POP_FRONT backends backend)
message(STATUS "Using backend: ${backend}.")

add_subdirectory("${backend}")

add_library(io_lib STATIC alloc.cc buf.cc file.cc)
sc_target(io_lib)
target_sources(
    io_lib PUBLIC FILE_SET file TYPE CXX_MODULES
    FILES alloc.ccm buf.ccm file.ccm owner.ccm
)

add_library(io STATIC close.cc interface.cc)
sc_target(io)
target_sources(io PUBLIC FILE_SET io TYPE CXX_MODULES FILES interface.ccm)
target_link_libraries(io PUBLIC io_impl)
