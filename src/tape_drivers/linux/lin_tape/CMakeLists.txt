add_library(tape_lintape SHARED
	lin_tape_ibmtape.c
	../../vendor_compat.c
	../../ibm_tape.c
	../../hp_tape.c
	../../quantum_tape.c
)
target_link_libraries(tape_lintape
	PkgConfig::FUSE
	ltfs
	libtape_linux_lin_tape
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape_lintape BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
