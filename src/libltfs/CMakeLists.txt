file(GLOB_RECURSE LIBLTFS_SRCS LIST_DIRECTORIES false "*.c")
add_library(libltfs SHARED ${LIBLTFS_SRCS})
set_property(TARGET libltfs PROPERTY OUTPUT_NAME ltfs)
target_include_directories(libltfs BEFORE PUBLIC "..")

target_link_libraries(libltfs
  Threads::Threads
  PkgConfig::FUSE
  LibXml2::LibXml2
  PkgConfig::UUID
  libbin_ltfs_messages
  liblibltfs_messages
  libinternal_error_messages
  libtape_common_messages
)

if(ENABLE_SNMP)
  target_link_libraries(libltfs PkgConfig::SNMP)
endif()
