add_library(tape_itdtimg SHARED
  itdtimg_tc.c
  ../../ibm_tape.c
)
target_link_libraries(tape_itdtimg
  PkgConfig::FUSE
  ltfs
  libtape_generic_itdtimg
  tape_reed_solomon_crc
  tape_crc32c_crc
)
target_include_directories(tape_itdtimg BEFORE PUBLIC "../..")
