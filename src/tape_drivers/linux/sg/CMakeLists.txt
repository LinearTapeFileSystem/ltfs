add_library(tape_sg SHARED
	sg_scsi_tape.c
	sg_tape.c
	../../vendor_compat.c
	../../ibm_tape.c
	../../hp_tape.c
	../../quantum_tape.c
	../../open_factor.c
)
target_link_libraries(tape_sg
	PkgConfig::FUSE
	ltfs
	libtape_linux_sg
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape_sg BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
