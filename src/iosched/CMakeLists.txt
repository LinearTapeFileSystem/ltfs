add_library(iosched_fcfs SHARED fcfs.c)
target_link_libraries(iosched_fcfs
  PkgConfig::FUSE
  ltfs
  libiosched_fcfs
)
target_include_directories(iosched_fcfs BEFORE PUBLIC "..")

add_library(iosched_unified SHARED
  unified.c
  cache_manager.c
)
target_link_libraries(iosched_unified
  PkgConfig::FUSE
  ltfs
  libiosched_unified
)
target_include_directories(iosched_unified BEFORE PUBLIC "..")
