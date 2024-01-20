target_sources(
    io_impl
    PRIVATE poll.cc pollable.cc
    PUBLIC FILE_SET interface TYPE CXX_MODULES FILES impl.ccm poll.ccm pollable.ccm
)
