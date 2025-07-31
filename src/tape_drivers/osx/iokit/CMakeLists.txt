add_library(tape_iokit SHARED
	scsipi_scsi_tape.c
	scsipi_ibmtape.c
	../../vendor_compat.c
	../../ibm_tape.c
	../../hp_tape.c
	../../quantum_tape.c
)
target_link_libraries(tape_iokit
	PkgConfig::FUSE
	ltfs
	libtape_iokit
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape_iokit BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
