add_library(io_impl STATIC sqe.cc uring.cc)
sc_target(io_impl)
target_sources(
    io_impl
    PUBLIC FILE_SET interface TYPE CXX_MODULES FILES impl.ccm sqe.ccm uring.ccm
)
target_link_libraries(io_impl PRIVATE co config io_lib lib)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
