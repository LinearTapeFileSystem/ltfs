# NOTE: This can be done using an .in file with @VARS@ but somehow this is done using sed
set(PLAT_OPTS)
if(ENABLE_LINTAPE)
	# list(APPEND PLAT_OPTS "plugin tape lin_tape ${CMAKE_INSTALL_LIBDIR}/ltfs/libtape_lin_tape.so")
	list(APPEND PLAT_OPTS "plugin tape lin_tape libtape_lin_tape.so")
endif()

if(LINUX)
	list(APPEND PLAT_OPTS "plugin tape sg libtape_sg.so")
elseif(APPLE)
	list(APPEND PLAT_OPTS "plugin tape iokit libtape_iokit.so")
elseif(BSD)
	if (CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
		list(APPEND PLAT_OPTS "plugin tape cam libtape-cam.so")
	elseif(CMAKE_SYSTEM_NAME STREQUAL "NetBSD")
		list(APPEND PLAT_OPTS "plugin tape scsipi-ibmtape libtape_scsipi_ibmtape.so")
	endif()
endif()

file(REAL_PATH "ltfs.conf.in" CONFIG_FILE)
file(COPY_FILE "ltfs.conf.local" "${CMAKE_CURRENT_BINARY_DIR}/ltfs.conf.local")
add_custom_command(OUTPUT "ltfs.conf"
	COMMAND sed -e "s!__PLATFORM_DRIVERS__!${PLAT_OPTS}!" ${CONFIG_FILE} > .tmp1
	COMMAND sed -e "s!__LIBDIR__!${CMAKE_INSTALL_LIBDIR}!" .tmp1 > .tmp2
	COMMAND sed -e "s!__DEFAULT_TAPE__!${DEFAULT_TAPE}!" .tmp2 > .tmp1
	COMMAND sed -e "s!__DEFAULT_IOSCHED__!${DEFAULT_IOSCHED}!" .tmp1 > .tmp2
	COMMAND sed -e "s!__DEFAULT_KMI__!${DEFAULT_KMI}!" .tmp2 > .tmp1
	COMMAND sed -e "s!__CONFDIR__!${CMAKE_CURRENT_BINARY_DIR}!" .tmp1 > ltfs.conf
	DEPENDS ${CONFIG_FILE}
	BYPRODUCTS .tmp1 .tmp2
)
add_custom_target("conf" ALL DEPENDS "ltfs.conf")
