set(_td "${CMAKE_SOURCE_DIR}/src/tape_drivers")

ltfs_add_plugin(libtape-iokit
    SOURCES
        iokit_tape.c
        iokit_scsi.c
        iokit_service.c
        ${_td}/vendor_compat.c
        ${_td}/ibm_tape.c
        ${_td}/hp_tape.c
        ${_td}/quantum_tape.c
    INCLUDES ${_td}
    LIBS ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK}
    CRC
    MSG tape_iokit)
