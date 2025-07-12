add_library(tape-itdtimg SHARED
	itdtimg_tc.c
	../../ibm_tape.c
)
target_link_libraries(tape-itdtimg
	PkgConfig::FUSE
	ltfs
	libtape_generic_itdtimg
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape-itdtimg BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
