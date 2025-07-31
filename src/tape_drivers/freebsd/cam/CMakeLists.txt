add_library(tape-cam SHARED
	cam_cmn.c
	cam_tc.c
	../../vendor_compat.c
	../../ibm_tape.c
	../../hp_tape.c
	../../quantum_tape.c
)
target_link_libraries(tape-cam
	PkgConfig::FUSE
	ltfs
	libtape_freebsd_cam
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape-cam BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
