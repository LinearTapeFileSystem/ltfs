set(LIBTAPE_CAM_SRCS
  cam_cmn.c
  cam_tc.c
  ../../vendor_compat.c
  ../../ibm_tape.c
  ../../hp_tape.c
  ../../quantum_tape.c
)
add_library(libtape_cam SHARED ${LIBTAPE_CAM_SRCS})
target_link_libraries(libtape_cam
  PkgConfig::FUSE
  libltfs
  libtape_freebsd_cam_messages
  libtape_reed_solomon_crc
  libtape_crc32c_crc
)
target_include_directories(libtape_cam BEFORE PUBLIC "../..")
