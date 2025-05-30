set(LIBIOSCHED_FCFS_SRCS fcfs.c)
add_library(libiosched_fcfs SHARED ${LIBIOSCHED_FCFS_SRCS})
target_link_libraries(libiosched_fcfs
  PkgConfig::FUSE
  libltfs
  libiosched_fcfs_messages
)
target_include_directories(libiosched_fcfs BEFORE PUBLIC "..")

set(LIBIOSCHED_UNIFIED_SRCS
  unified.c
  cache_manager.c
)
add_library(libiosched_unified SHARED ${LIBIOSCHED_UNIFIED_SRCS})
target_link_libraries(libiosched_unified
  PkgConfig::FUSE
  libltfs
  libiosched_unified_messages
)
target_include_directories(libiosched_unified BEFORE PUBLIC "..")
