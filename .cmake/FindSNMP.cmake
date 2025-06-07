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
