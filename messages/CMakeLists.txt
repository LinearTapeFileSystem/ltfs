# Compile the ICU message bundles. Platform bundles (lin_tape, iokit) are
# added when those backends build.
set(LTFS_MESSAGE_BUNDLES
    libltfs
    internal_error
    tape_common
    bin_ltfs
    bin_mkltfs
    bin_ltfsck
    iosched_fcfs
    iosched_unified
    kmi_simple
    kmi_flatfile
    tape_generic_file
    tape_generic_itdtimg)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND LTFS_MESSAGE_BUNDLES tape_linux_sg)
    if(LTFS_ENABLE_LINTAPE)
        list(APPEND LTFS_MESSAGE_BUNDLES tape_linux_lin_tape)
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND LTFS_MESSAGE_BUNDLES tape_iokit)
endif()

foreach(_b ${LTFS_MESSAGE_BUNDLES})
    ltfs_add_message_bundle(${_b})
endforeach()
