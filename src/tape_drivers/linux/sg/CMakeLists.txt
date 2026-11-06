set(_td "${CMAKE_SOURCE_DIR}/src/tape_drivers")

ltfs_add_plugin(libtape-sg
    SOURCES
        sg_scsi_tape.c
        sg_tape.c
        ${_td}/vendor_compat.c
        ${_td}/ibm_tape.c
        ${_td}/hp_tape.c
        ${_td}/quantum_tape.c
        ${_td}/open_factor.c
    INCLUDES ${_td}
    CRC
    MSG tape_linux_sg)
