add_executable(mkltfs mkltfs.c)
target_link_libraries(mkltfs
  ICU::uc
  libltfs
  libbin_mkltfs_messages
)
target_include_directories(mkltfs BEFORE PUBLIC ".")
target_compile_definitions(mkltfs PRIVATE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)

add_executable(ltfsck ltfsck.c)
target_link_libraries(ltfsck
  ICU::uc
  libltfs
  libbin_ltfsck_messages
)
target_include_directories(ltfsck BEFORE PUBLIC ".")
target_compile_definitions(ltfsck PRIVATE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)

add_executable(ltfsindextool ltfsindextool.c)
target_link_libraries(ltfsindextool
  ICU::uc
  libltfs
  libbin_ltfsindextool_messages
  libbin_mkltfs_messages
)
target_include_directories(ltfsindextool BEFORE PUBLIC ".")
target_compile_definitions(ltfsindextool PRIVATE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)
