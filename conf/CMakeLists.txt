# Generate ltfs.conf from ltfs.conf.in, reproducing the autotools multi-pass
# sed substitution (__PLATFORM_DRIVERS__, __LIBDIR__, __DEFAULT_*__,
# __CONFDIR__).
set(_plat_drivers "")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_plat_drivers "plugin tape sg __LIBDIR__/ltfs/libtape-sg.so")
    if(LTFS_ENABLE_LINTAPE)
        string(APPEND _plat_drivers "\nplugin tape lin_tape __LIBDIR__/ltfs/libtape-lin_tape.so")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(_plat_drivers "plugin tape iokit __LIBDIR__/ltfs/libtape-iokit.so")
elseif(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
    set(_plat_drivers "plugin tape cam __LIBDIR__/ltfs/libtape-cam.so")
elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
    set(_plat_drivers "plugin tape scsipi-ibmtape __LIBDIR__/ltfs/libtape-scsipi-ibmtape.so")
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ltfs.conf.in" _conf)
string(REPLACE "__PLATFORM_DRIVERS__" "${_plat_drivers}" _conf "${_conf}")
string(REPLACE "__LIBDIR__" "${CMAKE_INSTALL_FULL_LIBDIR}" _conf "${_conf}")
string(REPLACE "__DEFAULT_TAPE__" "${DEFAULT_TAPE}" _conf "${_conf}")
string(REPLACE "__DEFAULT_IOSCHED__" "${DEFAULT_IOSCHED}" _conf "${_conf}")
string(REPLACE "__DEFAULT_KMI__" "${DEFAULT_KMI}" _conf "${_conf}")
string(REPLACE "__CONFDIR__" "${CMAKE_INSTALL_FULL_SYSCONFDIR}" _conf "${_conf}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ltfs.conf" CONTENT "${_conf}")

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ltfs.conf"
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR})
# ltfs.conf.local holds user edits: install only when absent. The prefix is
# resolved at install time (escaped) so --prefix overrides are honored.
install(CODE "
    set(_confdir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}\")
    if(NOT EXISTS \"\${_confdir}/ltfs.conf.local\")
        file(INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/ltfs.conf.local\"
             DESTINATION \"\${_confdir}\")
    endif()")
