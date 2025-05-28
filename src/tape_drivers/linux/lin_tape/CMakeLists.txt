set(LIBTAPE_LINTAPE_SRCS
  lin_tape_ibmtape.c
  ../../vendor_compat.c
  ../../ibm_tape.c
  ../../hp_tape.c
  ../../quantum_tape.c
)
add_library(libtape_lintape SHARED ${LIBTAPE_LINTAPE_SRCS})
target_link_libraries(libtape_lintape
  PkgConfig::FUSE
  libltfs
  libtape_linux_lin_tape_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_lintape BEFORE PUBLIC "../..")
