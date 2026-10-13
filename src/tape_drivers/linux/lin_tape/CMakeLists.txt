set(_td "${CMAKE_SOURCE_DIR}/src/tape_drivers")

ltfs_add_plugin(libtape-lin_tape
    SOURCES
        lin_tape_ibmtape.c
        ${_td}/vendor_compat.c
        ${_td}/ibm_tape.c
        ${_td}/hp_tape.c
        ${_td}/quantum_tape.c
    INCLUDES ${_td}
    CRC
    MSG tape_linux_lin_tape)
