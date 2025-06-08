add_executable(ltfs_bin main.c ltfs_fuse.c)
target_link_libraries(ltfs_bin
  Threads::Threads
  PkgConfig::FUSE
  LibXml2::LibXml2
  PkgConfig::UUID
  ICU::uc
  ltfs
)
set_property(TARGET ltfs_bin PROPERTY OUTPUT_NAME ltfs)
target_include_directories(ltfs_bin BEFORE PUBLIC "." )
add_compile_definitions(
  LTFS_CONFIG_FILE="${CMAKE_INSTALL_FULL_SYSCONFDIR}/ltfs.conf"
  LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
  PACKAGE_VERSION="${VERSION}"
  PACKAGE_NAME="${PROJECT_NAME}"
)

add_subdirectory(libltfs)
add_subdirectory(iosched)
add_subdirectory(kmi)
add_subdirectory(tape_drivers)
add_subdirectory(tape_drivers/generic/file)
add_subdirectory(tape_drivers/generic/itdtimg)
add_subdirectory(utils)

option(ENABLE_LINTAPE "Support IBM's lin_tape driver or not" OFF)
if(ENABLE_LINTAPE)
  add_subdirectory(tape_drivers/linux/lin_tape)
endif()

if(LINUX)
  add_subdirectory(tape_drivers/linux/sg)
elseif(APPLE)
  add_subdirectory(tape_drivers/osx/iokit)
elseif(BSD)
  if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    add_subdirectory(tape_drivers/freebsd/cam)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    add_subdirectory(tape_drivers/netbsd/scsipi-ibmtape)
  endif()
endif()

option(ICU_6X "Force to use ICU6x (unorm2) functions" OFF)
string(REPLACE "." ";" ICU_VERSION_ARRAY ${ICU_VERSION})
list(GET ICU_VERSION_ARRAY 0 ICU_VERSION_MAJOR)
if(ICU_6X OR ICU_VERSION_MAJOR GREATER_EQUAL "56")
  add_compile_definitions(USE_UNORM2)
  message(STATUS "Using ICU6x funtions")
endif()
