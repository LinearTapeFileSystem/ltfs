set(LIBTAPE_FILE_SRCS
  filedebug_tc.c
  filedebug_conf_tc.c
  ../../ibm_tape.c
)
add_library(libtape_file SHARED ${LIBTAPE_FILE_SRCS})
target_link_libraries(libtape_file
  PkgConfig::FUSE
  libltfs
  libtape_generic_file_messages
)
target_include_directories(libtape_file BEFORE PUBLIC "../..")
