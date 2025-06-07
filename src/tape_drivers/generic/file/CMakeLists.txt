add_library(tape_file SHARED
  filedebug_tc.c
  filedebug_conf_tc.c
  ../../ibm_tape.c
)
target_link_libraries(tape_file
  PkgConfig::FUSE
  ltfs
  libtape_generic_file
)
target_include_directories(tape_file BEFORE PUBLIC "../..")
