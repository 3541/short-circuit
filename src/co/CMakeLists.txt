add_library(co STATIC resume.cc)
sc_target(co)
target_sources(
    co
    PUBLIC FILE_SET co TYPE CXX_MODULES FILES
    future.ccm
    generator.ccm
    nop.ccm
    resume.ccm
)

target_link_libraries(co PRIVATE lib)
