target_compile_definitions(io_lib PRIVATE SC_IO_BACKEND_FILE_FLAGS)
target_compile_definitions(io_impl PRIVATE SC_IO_BACKEND_FILE_FLAGS)

target_sources(
    io_impl

    PRIVATE
    init.cc
    buf_manager.cc
    sqe.cc
    uring.cc

    PUBLIC FILE_SET interface TYPE CXX_MODULES FILES
    buf_manager.ccm
    impl.ccm
    init.ccm
    sqe.ccm
    uring.ccm
)
target_link_libraries(io_impl PRIVATE uring)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
