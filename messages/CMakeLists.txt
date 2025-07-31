set(MESSAGES_LIBS
	bin_ltfs
	bin_ltfsck
	bin_ltfsindextool
	bin_mkltfs
	internal_error
	iosched_fcfs
	iosched_unified
	kmi_flatfile
	kmi_simple
	libltfs
	tape_common
	tape_freebsd_cam
	tape_generic_file
	tape_generic_itdtimg
	tape_iokit
	tape_linux_lin_tape
	tape_linux_sg
)
set(LIBS)
foreach(NAME IN LISTS MESSAGES_LIBS)
	file(GLOB SRCS "${NAME}/*.txt")
	set(OUT "lib${NAME}.a")
	list(APPEND LIBS ${OUT})

	make_directory("${CMAKE_CURRENT_BINARY_DIR}/res_${NAME}")
	add_custom_command(OUTPUT ${OUT}
		COMMAND ${ICU_GENRB_EXECUTABLE} -q ${SRCS} 1> /dev/null
		COMMAND ls *.res > paths.txt
		COMMAND ${ICU_PKGDATA_EXECUTABLE} -m static -p ${NAME} -q paths.txt 1> /dev/null
		COMMAND cp ${OUT} ..
		DEPENDS ${SRCS}
		BYPRODUCTS res_${NAME}
		WORKING_DIRECTORY res_${NAME}
	)

	# Define the library as an imported CMake lib
	add_library("lib${NAME}" STATIC IMPORTED GLOBAL)
	set_target_properties("lib${NAME}" PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/lib${NAME}.a")
	add_dependencies("lib${NAME}" "messages")
endforeach()
# Do this to not rebuild the entire message libraries every run
add_custom_target(messages ALL DEPENDS ${LIBS})
