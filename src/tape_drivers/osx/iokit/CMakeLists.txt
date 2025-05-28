set(LIBTAPE_IOKIT_SRCS
  scsipi_scsi_tape.c
  scsipi_ibmtape.c
  ../../vendor_compat.c
  ../../ibm_tape.c hp_tape.c
  ../../quantum_tape.c
)
add_library(libtape_iokit SHARED ${LIBTAPE_IOKIT_SRCS})
target_link_libraries(libtape_iokit
  PkgConfig::FUSE
  libltfs
  libtape_iokit_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_iokit BEFORE PUBLIC "../..")
