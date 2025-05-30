add_executable(mkltfs mkltfs.c)
target_link_libraries(mkltfs
  ICU::uc
  libltfs
  libbin_mkltfs_messages
)
target_include_directories(mkltfs BEFORE PUBLIC ".")

add_executable(ltfsck ltfsck.c)
target_link_libraries(ltfsck
  ICU::uc
  libltfs
  libbin_ltfsck_messages
)
target_include_directories(ltfsck BEFORE PUBLIC ".")

add_executable(ltfsindextool ltfsindextool.c)
target_link_libraries(ltfsindextool
  ICU::uc
  libltfs
  libbin_ltfsindextool_messages
  libbin_mkltfs_messages
)
target_include_directories(ltfsindextool BEFORE PUBLIC ".")
