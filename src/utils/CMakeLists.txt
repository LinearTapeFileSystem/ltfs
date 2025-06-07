add_executable(mkltfs mkltfs.c)
target_link_libraries(mkltfs
  ICU::uc
  ltfs
  libbin_mkltfs
)
target_include_directories(mkltfs BEFORE PUBLIC ".")

add_executable(ltfsck ltfsck.c)
target_link_libraries(ltfsck
  ICU::uc
  ltfs
  libbin_ltfsck
)
target_include_directories(ltfsck BEFORE PUBLIC ".")

add_executable(ltfsindextool ltfsindextool.c)
target_link_libraries(ltfsindextool
  ICU::uc
  ltfs
  libbin_ltfsindextool
  libbin_mkltfs
)
target_include_directories(ltfsindextool BEFORE PUBLIC ".")
