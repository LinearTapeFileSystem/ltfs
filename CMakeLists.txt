cmake_minimum_required(VERSION 3.20)
project(ltfs,
  # TODO: Find a better solution to mark this version as different from the official build
  VERSION "2.5.0.9999"
)
include(CheckSymbolExists REQUIRED)
include(CheckTypeSize REQUIRED)
include(CheckLibraryExists REQUIRED)
include(CheckIncludeFiles REQUIRED)
include(GNUInstallDirs REQUIRED)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_package(ICU COMPONENTS uc)
find_package(LibXml2 REQUIRED)

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

# TODO: Improve the use of SNMP, right now this flags are not added to the build.
# TODO: Test the use of SNMP
option(ENABLE_SNMP "Force to use ICU6x (unorm2) functions" OFF)
if(ENABLE_SNMP)
  pkg_check_modules(SNMP REQUIRED "net-snmp>=5.3" IMPORTED_TARGET)
  find_program(NETSNMP_CONFIG_BIN net-snmp-config REQUIRED)
  if(NETSNMP_CONFIG_BIN)
    exec_program(${NETSNMP_CONFIG_BIN} ARGS --cflags OUTPUT_VARIABLE _NETSNMP_CFLAGS)
    exec_program(${NETSNMP_CONFIG_BIN} ARGS --libs OUTPUT_VARIABLE _NETSNMP_LIBS)

    string(REGEX REPLACE "[\"\r\n]" " " _NETSNMP_CFLAGS "${_NETSNMP_CFLAGS}")
    string(REGEX REPLACE "[\"\r\n]" " " _NETSNMP_LIBS "${_NETSNMP_LIBS}")
    set(NETSNMP_CFLAGS ${_NETSNMP_CFLAGS} CACHE STRING "CFLAGS for net-snmp lib")
    set(NETSNMP_LIBS ${_NETSNMP_LIBS} CACHE STRING "linker options for net-snmp lib")
    set(NETSNMP_FOUND TRUE CACHE BOOL "net-snmp is found")
  else()
    set (NETSNMP_FOUND FALSE CACHE BOOL "net-snmp is not found")
  endif()
endif()


check_type_size(time_t SIZEOF_TIME_T)
add_compile_definitions(
  SIZEOF_TIME_T=${SIZEOF_TIME_T}
)
