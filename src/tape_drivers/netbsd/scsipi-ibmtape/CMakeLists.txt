set(LIBTAPE_SG_IBMTAPE_SRCS
  scsipi_scsi_tape.c
  scsipi_ibmtape.c
  ../../vendor_compat.c
  ../../ibm_tape.c
  ../../hp_tape.c
  ../../quantum_tape.c
)
add_library(libtape_sg_ibmtape SHARED ${LIBTAPE_SG_IBMTAPE_SRCS})
target_link_libraries(libtape_sg_ibmtape
  PkgConfig::FUSE
  libltfs
  libtape_linux_sg_ibmtape_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_sg_ibmtape BEFORE PUBLIC "../..")
