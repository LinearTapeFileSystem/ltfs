add_library(libtape_sg_ibmtape SHARED
	scsipi_scsi_tape.c
	scsipi_ibmtape.c
	../../vendor_compat.c
	../../ibm_tape.c
	../../hp_tape.c
	../../quantum_tape.c
)
target_link_libraries(libtape_sg_ibmtape
	PkgConfig::FUSE
	ltfs
	libtape_linux_sg_ibmtape
	tape_reed_solomon_crc
	tape_crc32c_crc
)
target_include_directories(tape_sg_ibmtape BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
