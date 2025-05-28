# Define the project targets
add_executable(ltfs main.c ltfs_fuse.c)
target_link_libraries(ltfs
  Threads::Threads
  PkgConfig::FUSE
  PkgConfig::XML2
  PkgConfig::UUID
  ICU::uc
  libltfs
)
target_include_directories(ltfs BEFORE PUBLIC "." )
target_compile_definitions(ltfs PRIVATE
  _GNU_SOURCE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)
