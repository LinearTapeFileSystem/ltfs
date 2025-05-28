cmake_minimum_required(VERSION 3.20)
project(libltfs,
  VERSION 2.5.0.0
)

file(GLOB_RECURSE LIBLTFS_SRCS LIST_DIRECTORIES false "*.c")
add_library(libltfs SHARED ${LIBLTFS_SRCS})
target_include_directories(libltfs BEFORE PUBLIC ".." )

target_link_libraries(libltfs
  Threads::Threads
  PkgConfig::FUSE
  LibXml2::LibXml2
  PkgConfig::UUID
  libbin_ltfs
  liblibltfs
  libinternal_error
  libtape_common
)

if(ENABLE_SNMP)
  target_link_libraries(libltfs PkgConfig::SNMP)
endif()

target_compile_definitions(libltfs PRIVATE
  _GNU_SOURCE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)
