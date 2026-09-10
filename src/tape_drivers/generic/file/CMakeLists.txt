set(_td "${CMAKE_SOURCE_DIR}/src/tape_drivers")

ltfs_add_plugin(libtape-file
    SOURCES
        filedebug_tc.c
        filedebug_conf_tc.c
        ${_td}/ibm_tape.c
    INCLUDES ${_td}
    MSG tape_generic_file)
