add_library(co STATIC)
sc_target(co)
target_sources(co PUBLIC FILE_SET co TYPE CXX_MODULES FILES future.ccm generator.ccm)
