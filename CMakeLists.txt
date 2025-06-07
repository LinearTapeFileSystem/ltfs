cmake_minimum_required(VERSION 3.26)
project(ltfs
  LANGUAGES C)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.cmake")
set(VERSION "2.5.0.0 (Prelim cmake)")

include(CheckSymbolExists REQUIRED)
include(CheckTypeSize REQUIRED)
include(CheckLibraryExists REQUIRED)
include(CheckIncludeFiles REQUIRED)
include(GNUInstallDirs REQUIRED)

find_package(SNMP REQUIRED)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ICU COMPONENTS uc)
find_package(LibXml2 REQUIRED)

check_type_size(time_t SIZEOF_TIME_T)
add_compile_definitions(_GNU_SOURCE __CMAKE_BUILD)

pkg_check_modules(FUSE REQUIRED "fuse>=2.6.0" IMPORTED_TARGET)
target_compile_definitions(PkgConfig::FUSE INTERFACE
  _FILE_OFFSET_BITS=64
)

if(APPLE)
    pkg_check_modules(UUID REQUIRED "uuid>=1.6" IMPORTED_TARGET)
else()
    pkg_check_modules(UUID REQUIRED "uuid>=1.36" IMPORTED_TARGET)
endif()

add_subdirectory(messages)
add_subdirectory(src)

set(prefix ${CMAKE_INSTALL_PREFIX} CACHE INTERNAL "Base installation directory (equivalent to Autotools' prefix)")
set(exec_prefix ${CMAKE_INSTALL_FULL_BINDIR} CACHE INTERNAL "Directory for architecture-dependent executables (like Autotools' exec_prefix)")
set(libdir ${CMAKE_INSTALL_FULL_LIBDIR} CACHE INTERNAL "Directory for installing libraries (like Autotools' libdir)")
set(includedir ${CMAKE_INSTALL_FULL_INCLUDEDIR} CACHE INTERNAL "Directory for installing header files (like Autotools' includedir)")
set(PACKAGE ${PROJECT_NAME} CACHE INTERNAL "Package name (equivalent to Autotools' PACKAGE)")
configure_file(ltfs.pc.in ltfs.pc)
