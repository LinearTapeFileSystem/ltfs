set(LIBTAPE_SG_SRCS
  sg_scsi_tape.c
  sg_tape.c
  ../../vendor_compat.c
  ../../ibm_tape.c
  ../../hp_tape.c
  ../../quantum_tape.c
  ../../open_factor.c
)
add_library(libtape_sg SHARED ${LIBTAPE_SG_SRCS})
target_link_libraries(libtape_sg
  PkgConfig::FUSE
  libltfs
  libtape_linux_sg_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_sg BEFORE PUBLIC "../..")
