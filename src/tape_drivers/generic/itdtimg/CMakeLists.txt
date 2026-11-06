ltfs_add_plugin(libtape-itdtimg
    SOURCES itdtimg_tc.c
    INCLUDES ${CMAKE_SOURCE_DIR}/src/tape_drivers
    MSG tape_generic_itdtimg)
