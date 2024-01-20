add_library(io_lib STATIC addr.cc alloc.cc buf.cc error.cc file.cc)
sc_target(io_lib)
target_sources(
    io_lib PUBLIC FILE_SET file TYPE CXX_MODULES
    FILES addr.ccm alloc.ccm buf.ccm close.ccm error.ccm file.ccm owner.ccm
)
target_link_libraries(io_lib PRIVATE config lib)

add_subdirectory(impl)

add_library(io INTERFACE)
target_link_libraries(io INTERFACE io_lib io_impl)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
