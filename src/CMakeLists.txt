# Define the project targets
add_executable(ltfs main.c ltfs_fuse.c)
target_link_libraries(ltfs
  Threads::Threads
  PkgConfig::FUSE
  LibXml2::LibXml2
  PkgConfig::UUID
  ICU::uc
  libltfs
)
target_include_directories(ltfs BEFORE PUBLIC "." )
target_compile_definitions(ltfs PRIVATE
  LTFS_CONFIG_FILE="${SYSCONFDIR}/ltfs.conf"
  LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
  PACKAGE_VERSION="${PROJECT_VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)

add_subdirectory(libltfs)

# NOTE: Is this needed? cmake can add this feature using -DUSE_UNORM2
option(ICU_6X "Force to use ICU6x (unorm2) functions" OFF)
string(REPLACE "." ";" ICU_VERSION_ARRAY ${ICU_VERSION})
list(GET ICU_VERSION_ARRAY 0 ICU_VERSION_MAJOR)
if(ICU_6X OR ICU_VERSION_MAJOR GREATER_EQUAL "56")
  add_compile_definitions(USE_UNORM2)
  message(STATUS "Using ICU6x funtions")
endif()
