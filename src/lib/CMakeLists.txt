add_library(
    lib
    STATIC

    include/sc/lib/defer.hh
    include/sc/lib/fwd.hh
    include/sc/lib/option.hh
    include/sc/lib/try.hh

    error.cc
)

sc_target(lib)
target_include_directories(lib PUBLIC include)
target_sources(
    lib PUBLIC FILE_SET lib TYPE CXX_MODULES
    FILES
    bind.ccm
    cast.ccm
    concepts.ccm
    constant.ccm
    deque.ccm
    error.ccm
    expected.ccm
    nonnull.ccm
    stream.ccm
    tuple.ccm
    tvec.ccm
    variant.ccm
    wrap.ccm
)
target_link_libraries(lib PRIVATE a3)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
