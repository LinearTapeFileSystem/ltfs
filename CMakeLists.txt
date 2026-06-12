#
# CMake build for LTFS. Coexists with the autotools build; both are
# maintained. The package version is read from the top-level VERSION file
# (the single source of truth shared with configure.ac).
#
cmake_minimum_required(VERSION 3.18)

# --- Version (single source of truth: ./VERSION) -----------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _ltfs_ver_lines)
list(GET _ltfs_ver_lines 0 LTFS_VERSION_NUM)
list(LENGTH _ltfs_ver_lines _ltfs_ver_n)
set(LTFS_VERSION_SUFFIX "")
if(_ltfs_ver_n GREATER 1)
    list(GET _ltfs_ver_lines 1 LTFS_VERSION_SUFFIX)
endif()
if(LTFS_VERSION_SUFFIX STREQUAL "")
    set(LTFS_VERSION_FULL "${LTFS_VERSION_NUM}")
else()
    set(LTFS_VERSION_FULL "${LTFS_VERSION_NUM} ${LTFS_VERSION_SUFFIX}")
endif()

project(ltfs VERSION ${LTFS_VERSION_NUM} LANGUAGES C)

set(LTFS_PACKAGE_NAME "LTFS")
set(LTFS_PACKAGE_TARNAME "ltfs")
set(LTFS_BUGREPORT "IBM corporation.")

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

include(GNUInstallDirs)
include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckTypeSize)
include(CheckCCompilerFlag)

# --- Options (mirror the autotools --enable/--with switches) ------------------
option(LTFS_ENABLE_SNMP "Build with SNMP support" ON)
option(LTFS_ENABLE_LINTAPE "Build the IBM lin_tape backend (Linux only)" OFF)
option(LTFS_SUPPORT_BUGGY_IFS "Work around buggy tape interfaces" OFF)
option(LTFS_LIVELINK "Enable livelink symlink mode" ON)
option(LTFS_MESSAGE_CHECK "Compile in message-checking mode" OFF)
option(LTFS_WERROR "Treat warnings as errors" OFF)
option(LTFS_DEBUG "Compile with extra debugging output" OFF)

# New locking defaults ON for NetBSD (its old locking is broken), OFF elsewhere.
if(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    option(LTFS_NEW_LOCKING "Use the new locking implementation" ON)
else()
    option(LTFS_NEW_LOCKING "Use the new locking implementation" OFF)
endif()

# Default plugin selection, matching configure.ac.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_default_tape "sg")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_default_tape "iokit")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(_default_tape "cam")
elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    set(_default_tape "scsipi-ibmtape")
endif()
set(DEFAULT_TAPE "${_default_tape}" CACHE STRING "Default tape backend plugin")
set(DEFAULT_IOSCHED "unified" CACHE STRING "Default I/O scheduler plugin")
set(DEFAULT_KMI "none" CACHE STRING "Default key manager plugin")

# --- Dependencies ------------------------------------------------------------
# find_package is used wherever a CMake package exists (LibXml2, ICU, Threads).
# libfuse, libuuid, and net-snmp ship no CMake config; pkg-config is their
# canonical interface, wrapped as IMPORTED_TARGETs (PkgConfig::*).
find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse>=2.6.0)

find_package(LibXml2 2.6.16 REQUIRED)

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid>=1.6)
else()
    pkg_check_modules(UUID REQUIRED IMPORTED_TARGET uuid>=1.36)
endif()

# ICU via the CMake module (replaces the removed icu-config).
find_package(ICU REQUIRED COMPONENTS uc i18n io data)
set(LTFS_ICU6X OFF)
if(ICU_VERSION VERSION_GREATER_EQUAL "60")
    set(LTFS_ICU6X ON)
endif()

find_package(Threads REQUIRED)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(RT_LIBRARY rt)
endif()

# SNMP (optional).
if(LTFS_ENABLE_SNMP)
    pkg_check_modules(NETSNMP IMPORTED_TARGET net-snmp>=5.3)
    if(NOT NETSNMP_FOUND)
        message(WARNING "net-snmp not found via pkg-config; disabling SNMP")
        set(LTFS_ENABLE_SNMP OFF)
    endif()
endif()

# Platform-specific libraries (declared for all platforms; used where built).
# backtrace() has a CMake module (libc on glibc, libexecinfo on the BSDs);
# cam/bsdxml/mt and the Apple frameworks ship neither CMake configs nor
# pkg-config files, so find_library is the canonical way to locate them.
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    find_library(CAM_LIBRARY cam REQUIRED)
    find_library(BSDXML_LIBRARY bsdxml REQUIRED)
    find_library(MT_LIBRARY mt REQUIRED)
    find_package(Backtrace REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    find_package(Backtrace REQUIRED)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
endif()

# ICU host tools for the message bundles.
find_program(GENRB genrb REQUIRED)
find_program(PKGDATA pkgdata REQUIRED)

# --- Feature checks for config.h ---------------------------------------------
check_include_file("sys/sysctl.h" HAVE_SYS_SYSCTL_H)
check_include_file("fcntl.h"      HAVE_FCNTL_H)
check_include_file("limits.h"     HAVE_LIMITS_H)
check_include_file("stddef.h"     HAVE_STDDEF_H)
check_include_file("stdint.h"     HAVE_STDINT_H)
check_include_file("stdlib.h"     HAVE_STDLIB_H)
check_include_file("string.h"     HAVE_STRING_H)
check_include_file("sys/ioctl.h"  HAVE_SYS_IOCTL_H)
check_include_file("sys/mount.h"  HAVE_SYS_MOUNT_H)
check_include_file("sys/time.h"   HAVE_SYS_TIME_H)
check_include_file("termios.h"    HAVE_TERMIOS_H)
check_include_file("unistd.h"     HAVE_UNISTD_H)
check_type_size("time_t" SIZEOF_TIME_T)

# xmlTextReaderSetup and XML_PARSE_HUGE (need libxml2 flags in scope).
# XML_PARSE_HUGE is an enumerator, not a macro, so it needs a compile test.
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${LIBXML2_INCLUDE_DIRS})
set(CMAKE_REQUIRED_LIBRARIES ${LIBXML2_LIBRARIES})
check_symbol_exists(xmlTextReaderSetup "libxml/xmlreader.h" HAVE_XML_READER_SETUP)
check_c_source_compiles("
#include <libxml/parser.h>
int main(void) { int foo = XML_PARSE_HUGE; (void)foo; return 0; }"
    HAVE_XML_PARSE_HUGE)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

# --- Common compile settings -------------------------------------------------
# Definitions every translation unit gets, mirroring AM_CPPFLAGS/AM_CFLAGS.
add_compile_definitions(
    _GNU_SOURCE
    LTFS_CONFIG_FILE="${CMAKE_INSTALL_FULL_SYSCONFDIR}/ltfs.conf"
    LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
)
if(LTFS_ENABLE_SNMP)
    add_compile_definitions(ENABLE_SNMP)
endif()
if(LTFS_NEW_LOCKING)
    add_compile_definitions(USE_NEW_LOCKING)
endif()
if(NOT LTFS_LIVELINK)
    add_compile_definitions(POSIXLINK_ONLY)
endif()
if(LTFS_MESSAGE_CHECK)
    add_compile_definitions(MSG_CHECK)
endif()
if(LTFS_DEBUG)
    add_compile_definitions(DEBUG TRACE)
endif()
if(LTFS_ICU6X)
    add_compile_definitions(ICU6x)
endif()
if(LTFS_SUPPORT_BUGGY_IFS AND (CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin"))
    add_compile_definitions(SUPPORT_BUGGY_IFS)
endif()

# -Wno-unused-parameter: callback signatures (FUSE operations, plugin ops)
# must keep parameters they do not use.
# -Wno-missing-field-initializers: designated initializers of operation and
# option tables intentionally leave the remaining members zeroed.
add_compile_options(-Wall -Wextra -Wno-unused-parameter
                    -Wno-missing-field-initializers -Wsign-compare -fsigned-char)
if(LTFS_WERROR)
    add_compile_options(-Werror -Wno-deprecated-declarations)
endif()

# config.h (binary dir) and the src tree on the include path for every target.
include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)

# CRC files need SSE4.2 on capable x86; detected once, applied to the ltfs_crc
# object library (see src/tape_drivers).
set(LTFS_CRC_OPTIONS "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|i[3-6]86")
    check_c_compiler_flag("-msse4.2" LTFS_HAVE_MSSE42)
    if(LTFS_HAVE_MSSE42)
        set(LTFS_CRC_OPTIONS -msse4.2 -D__SSE42__)
    endif()
endif()

# Helper modules.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(LtfsMessages)
include(LtfsPlugin)

enable_testing()

# --- Subprojects -------------------------------------------------------------
add_subdirectory(messages)
add_subdirectory(src)
add_subdirectory(conf)
add_subdirectory(man)
add_subdirectory(init.d)
add_subdirectory(tests)

# pkg-config file (reuse the autotools template).
set(prefix "${CMAKE_INSTALL_PREFIX}")
set(exec_prefix "\${prefix}")
set(libdir "${CMAKE_INSTALL_FULL_LIBDIR}")
set(includedir "${CMAKE_INSTALL_FULL_INCLUDEDIR}")
set(PACKAGE "ltfs")
set(VERSION "${LTFS_VERSION_FULL}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ltfs.pc.in
               ${CMAKE_CURRENT_BINARY_DIR}/ltfs.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/ltfs.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
