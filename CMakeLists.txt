cmake_minimum_required(VERSION 3.20)
project(ltfs,
  VERSION 2.5.0.0
)
include(CheckSymbolExists REQUIRED)
include(CheckTypeSize REQUIRED)
include(CheckLibraryExists REQUIRED)
include(CheckIncludeFiles REQUIRED)
include(GNUInstallDirs REQUIRED)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ICU COMPONENTS uc)

# pkg_check_modules(DL REQUIRED "libdl" IMPORTED_TARGET)
pkg_check_modules(FUSE REQUIRED "fuse>=2.6.0" IMPORTED_TARGET)
target_compile_definitions(PkgConfig::FUSE INTERFACE _FILE_OFFSET_BITS=64)

pkg_check_modules(XML2 REQUIRED "libxml-2.0>=2.6.16" IMPORTED_TARGET)

if(APPLE)
    pkg_check_modules(UUID REQUIRED "uuid>=1.6" IMPORTED_TARGET)
else()
    pkg_check_modules(UUID REQUIRED "uuid>=1.36" IMPORTED_TARGET)
endif()

add_subdirectory(messages)
add_subdirectory(src/libltfs)
add_subdirectory(src)


# Parsing features options
# NOTE: Is this needed? cmake can add this feature using -DUSE_UNORM2
# option(ICU_6X "Force to use ICU6x (unorm2) functions" OFF)
# if(ICU_6X)
#   add_definitions(-DUSE_UNORM2)
# endif()

# option(SNMP "Force to use ICU6x (unorm2) functions" OFF)
if(ENABLE_SNMP)
  pkg_check_modules(SNMP REQUIRED "net-snmp>=5.3" IMPORTED_TARGET)
endif()

# # Check libraries exists
# check_library_exists(pthread pthread_create "" HAVE_PTHREAD)
# if(APPLE)
#   check_library_exists(rt clock_gettime "" HAVE_RT)
# endif()

# if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
#   check_library_exists(cam cam_open_device "" HAVE_CAM)
#   check_library_exists(bsdxml XML_ParserCreate "" HAVE_BSDXML)
#   check_library_exists(mt mt_status_free "" HAVE_MT)
#   check_library_exists(execinfo backtrace "" HAVE_EXECINFO)
# elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
#   check_library_exists(execinfo backtrace "" HAVE_EXECINFO)
# endif()

# # Compilation requirements
# # NOTE: Checking for standard headers is really needed?
# set(
#   STD_HEADERS
#   "fcntl.h"
#   "limits.h"
#   "stddef.h"
#   "stdint.h"
#   "stdlib.h"
#   "stdbool.h"
#   "string.h"
#   "sys/ioctl.h"
#   "sys/mount.h"
#   "sys/time.h"
#   "termios.h"
#   "unistd.h"
#   "sys/sysctl.h"
# )
# check_include_files("${STD_HEADERS}" HAVE_STD_HEADERS)
# check_type_size(mode_t MODE_T)
# check_type_size(off_t OFF_T)
# check_type_size(size_t SIZE_T)
# check_type_size(ssize_t SSIZE_T)
# check_type_size(time_t TIME_T)
# check_type_size(uint8_t UINT8_T)
# check_type_size(uint16_t UINT16_T)
# check_type_size(uint32_t UINT32_T)
# check_type_size(uint64_t UINT64_T)
# check_type_size(uint64_t UINT64_T)
# check_symbol_exists(malloc "stdlib.h" HAVE_MALLOC)
# check_symbol_exists(strlen "string.h" HAVE_STRLEN)
# check_symbol_exists(bzero "string.h" HAVE_BZERO)
# check_symbol_exists(memchr "string.h" HAVE_MEMCHR)
# check_symbol_exists(memset "string.h" HAVE_MEMSET)
# check_symbol_exists(strcasecmp "string.h" HAVE_STRCASECMP)
# check_symbol_exists(strdup "string.h" HAVE_STRDUP)
# check_symbol_exists(strerror "string.h" HAVE_STRERROR)
# check_symbol_exists(strndup "string.h" HAVE_STRNDUP)
# check_symbol_exists(strrchr "string.h" HAVE_STRRCHR)
# check_symbol_exists(strstr "string.h" HAVE_STRSTR)
# check_symbol_exists(mkdir "sys/stat.h" HAVE_MKDIR)
# check_symbol_exists(rmdir "unistd.h" HAVE_RMDIR)


# check_library_exists(xml2 xmlTextReaderSetup "" HAVE_XML_TEXT_READER_SETUP)
# if(HAVE_XML_TEXT_READER_SETUP)
#   # TODO: Add add macro definitions
# endif()

# # check_symbol_exists(xmlTextReaderSetup "" HAVE_XML_TEXT_READER_SETUP)
