#
# CMake build for LTFS. Coexists with the autotools build; both are
# maintained. The package version is read from the top-level VERSION file
# (the single source of truth shared with configure.ac).
#
cmake_minimum_required(VERSION 3.18)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds would overwrite autotools files; use a build directory, e.g. cmake -S . -B build")
endif()

# --- Version (single source of truth: ./VERSION) -----------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" _ltfs_ver_lines)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
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

if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    message(WARNING "The CMake build is not tested on ${CMAKE_SYSTEM_NAME}.")
endif()

set(LTFS_PACKAGE_NAME "LTFS")
set(LTFS_PACKAGE_TARNAME "ltfs")
set(LTFS_BUGREPORT "IBM corporation.")

set(CMAKE_C_STANDARD 11)
#set(CMAKE_C_STANDARD 17)   # bump to C17 once the oldest supported toolchains are GCC 8+/Clang 7+
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)   # gnu11: LTFS uses GNU extensions (_GNU_SOURCE, asprintf, ...)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

include(GNUInstallDirs)

# Keep the install libdir in the RUNPATH of the installed binaries and
# plugins, as the libtool build does, unless it is a system directory.
if("${CMAKE_INSTALL_RPATH}" STREQUAL "")
    list(FIND CMAKE_C_IMPLICIT_LINK_DIRECTORIES "${CMAKE_INSTALL_FULL_LIBDIR}" _ltfs_system_libdir)
    if(_ltfs_system_libdir EQUAL -1)
        set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_FULL_LIBDIR}")
    endif()
endif()

include(CheckIncludeFile)
include(CheckSymbolExists)
include(CheckTypeSize)
include(CheckCCompilerFlag)

# --- Options (mirror the autotools --enable/--with switches) ------------------
set(LTFS_WITH_FUSE2 "AUTO" CACHE STRING "Use libfuse 2 instead of 3 (ON/OFF/AUTO)")
set_property(CACHE LTFS_WITH_FUSE2 PROPERTY STRINGS AUTO ON OFF)
# SNMP is not supported on macOS (README: configure --disable-snmp there).
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_ltfs_snmp_default OFF)
else()
    set(_ltfs_snmp_default ON)
endif()
option(LTFS_ENABLE_SNMP "Build with SNMP support" ${_ltfs_snmp_default})
option(LTFS_ENABLE_LINTAPE "Build the IBM lin_tape backend (Linux only)" OFF)
option(LTFS_SUPPORT_BUGGY_IFS "Work around buggy tape interfaces" OFF)
option(LTFS_LIVELINK "Enable livelink symlink mode" ON)
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
if(DEFAULT_TAPE STREQUAL "lin_tape" AND NOT LTFS_ENABLE_LINTAPE)
    message(NOTICE "DEFAULT_TAPE=lin_tape: enabling LTFS_ENABLE_LINTAPE")
    set(LTFS_ENABLE_LINTAPE ON CACHE BOOL "Build the IBM lin_tape backend (Linux only)" FORCE)
endif()
set(DEFAULT_IOSCHED "unified" CACHE STRING "Default I/O scheduler plugin")
set(DEFAULT_KMI "none" CACHE STRING "Default key manager plugin")

# --- Dependencies ------------------------------------------------------------
# find_package is used wherever a CMake package exists (LibXml2, ICU, Threads).
# libfuse, libuuid, and net-snmp ship no CMake config; pkg-config is their
# canonical interface, wrapped as IMPORTED_TARGETs (PkgConfig::*).
find_package(PkgConfig REQUIRED)

set(LTFS_HAVE_FUSE3 OFF)
if(LTFS_WITH_FUSE2 STREQUAL "ON")
    pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse>=2.6.0)
elseif(LTFS_WITH_FUSE2 STREQUAL "OFF")
    pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse3>=3.4.0)
    set(LTFS_HAVE_FUSE3 ON)
elseif(LTFS_WITH_FUSE2 STREQUAL "AUTO")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        pkg_check_modules(FUSE IMPORTED_TARGET fuse3>=3.4.0)
        if(FUSE_FOUND)
            set(LTFS_HAVE_FUSE3 ON)
        else()
            pkg_check_modules(FUSE IMPORTED_TARGET fuse>=2.6.0)
            if(NOT FUSE_FOUND)
                message(FATAL_ERROR "neither fuse3 >= 3.4.0 nor fuse >= 2.6.0 was found. "
                    "Install the libfuse 3 development package (libfuse3-dev or fuse3-devel) "
                    "or the libfuse 2 one (libfuse-dev or fuse-devel)")
            endif()
        endif()
    else()
        pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse>=2.6.0)
    endif()
else()
    message(FATAL_ERROR "LTFS_WITH_FUSE2 must be AUTO, ON or OFF, not '${LTFS_WITH_FUSE2}'")
endif()
if(LTFS_HAVE_FUSE3)
    message(STATUS "LTFS: building against FUSE 3 (${FUSE_VERSION})")
else()
    message(STATUS "LTFS: building against FUSE 2 (${FUSE_VERSION})")
endif()

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
    pkg_check_modules(NETSNMP IMPORTED_TARGET netsnmp-agent>=5.3)
    if(NETSNMP_FOUND)
        set(LTFS_SNMP_LIBS PkgConfig::NETSNMP)
    else()
        find_program(NET_SNMP_CONFIG net-snmp-config)
        if(NET_SNMP_CONFIG)
            execute_process(COMMAND ${NET_SNMP_CONFIG} --agent-libs
                            RESULT_VARIABLE _snmp_result
                            OUTPUT_VARIABLE _snmp_libs
                            OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(_snmp_result EQUAL 0 AND NOT _snmp_libs STREQUAL "")
                separate_arguments(LTFS_SNMP_LIBS UNIX_COMMAND "${_snmp_libs}")
                # net-snmp-config repeats -lm and -lssl; CMake would then also repeat the
                # message objects (plain object files) on the link line.
                list(REMOVE_DUPLICATES LTFS_SNMP_LIBS)
            endif()
        endif()
    endif()
    if(NOT LTFS_SNMP_LIBS)
        message(FATAL_ERROR "net-snmp development files not found; install them or configure with -DLTFS_ENABLE_SNMP=OFF")
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
if(NOT ICU_GENRB_EXECUTABLE OR NOT ICU_PKGDATA_EXECUTABLE)
    message(FATAL_ERROR "ICU genrb and pkgdata not found; install the ICU tools (e.g. icu-devtools)")
endif()

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

# struct fuse_file_info.parallel_direct_writes appeared in libfuse 3.15;
# detect the member instead of relying on version numbers. It is a bitfield,
# so sizeof-based probes (check_struct_has_member) cannot be used. The cached
# result is dropped so that a switch of LTFS_WITH_FUSE2 probes again.
unset(HAVE_FUSE_PARALLEL_DIRECT_WRITES CACHE)
if(LTFS_HAVE_FUSE3)
    set(CMAKE_REQUIRED_INCLUDES ${FUSE_INCLUDE_DIRS})
    set(CMAKE_REQUIRED_DEFINITIONS -DFUSE_USE_VERSION=31 -D_FILE_OFFSET_BITS=64)
    check_c_source_compiles("
#include <fuse.h>
int main(void) { struct fuse_file_info fi = {0}; fi.parallel_direct_writes = 1; return 0; }"
        HAVE_FUSE_PARALLEL_DIRECT_WRITES)
    unset(CMAKE_REQUIRED_INCLUDES)
    unset(CMAKE_REQUIRED_DEFINITIONS)
endif()

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/config.h" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ltfs")

# --- Common compile settings -------------------------------------------------
# Definitions every translation unit gets, mirroring AM_CPPFLAGS/AM_CFLAGS.
add_compile_definitions(
    _GNU_SOURCE
    LTFS_CONFIG_FILE="${CMAKE_INSTALL_FULL_SYSCONFDIR}/ltfs.conf"
    LTFS_BASE_DIR="${CMAKE_INSTALL_PREFIX}"
)
if(LTFS_HAVE_FUSE3)
    # fuse3.pc does not add the 64-bit off_t define that fuse.pc adds
    add_compile_definitions(HAVE_FUSE3 _FILE_OFFSET_BITS=64)
endif()
if(LTFS_ENABLE_SNMP)
    add_compile_definitions(ENABLE_SNMP)
endif()
if(LTFS_NEW_LOCKING)
    add_compile_definitions(USE_NEW_LOCKING)
endif()
if(NOT LTFS_LIVELINK)
    add_compile_definitions(POSIXLINK_ONLY)
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
                    -Wno-missing-field-initializers -Wsign-compare -fsigned-char
                    -fno-strict-aliasing)
if(LTFS_WERROR)
    add_compile_options(-Werror -Wno-deprecated-declarations)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 8)
        add_compile_options(-Wno-stringop-truncation)
    endif()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    add_link_options(LINKER:--no-undefined LINKER:--as-needed)
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
