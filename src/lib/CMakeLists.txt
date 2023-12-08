add_library(lib STATIC include/sc/lib/fwd.hh include/sc/lib/option.hh)
sc_target(lib)
target_include_directories(lib PUBLIC include)
target_sources(
    lib PUBLIC FILE_SET lib TYPE CXX_MODULES
    FILES
    cast.ccm
    constant.ccm
    deque.ccm
    error.ccm
    nonnull.ccm
)
target_link_libraries(lib PRIVATE a3)

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
