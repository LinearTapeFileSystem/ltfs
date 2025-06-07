add_library(tape_cam SHARED
  cam_cmn.c
  cam_tc.c
  ../../vendor_compat.c
  ../../ibm_tape.c
  ../../hp_tape.c
  ../../quantum_tape.c
)
target_link_libraries(tape_cam
  PkgConfig::FUSE
  ltfs
  libtape_freebsd_cam
  tape_reed_solomon_crc
  tape_crc32c_crc
)
target_include_directories(tape_cam BEFORE PUBLIC "../..")
