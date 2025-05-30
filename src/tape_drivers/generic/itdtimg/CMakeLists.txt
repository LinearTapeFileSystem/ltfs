set(LIBTAPE_ITDTIMG_SRCS
  itdtimg_tc.c
  ../../ibm_tape.c
)
add_library(libtape_itdtimg SHARED ${LIBTAPE_ITDTIMG_SRCS})
target_link_libraries(libtape_itdtimg
  PkgConfig::FUSE
  libltfs
  libtape_generic_itdtimg_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_itdtimg BEFORE PUBLIC "../..")
