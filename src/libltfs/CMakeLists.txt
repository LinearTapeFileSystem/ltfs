# The core LTFS library: a versioned shared object that the executables and
# all runtime plugins link against.
add_library(ltfs SHARED
    ltfs.c
    ltfs_internal.c
    ltfs_fsops.c
    ltfs_fsops_raw.c
    ltfssnmp.c
    fs.c
    xml_common.c
    xml_writer.c
    xml_reader.c
    xml_writer_libltfs.c
    xml_reader_libltfs.c
    label.c
    base64.c
    tape.c
    iosched.c
    dcache.c
    kmi.c
    pathname.c
    index_criteria.c
    xattr.c
    ltfslogging.c
    ltfstrace.c
    ltfs_thread.c
    config_file.c
    plugin.c
    periodic_sync.c
    arch/uuid_internal.c
    arch/filename_handling.c
    arch/time_internal.c
    arch/arch_info.c
    arch/errormap.c)

set_target_properties(ltfs PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# Dependencies are PUBLIC so executables and plugins that link ltfs inherit
# the include paths and link flags, mirroring the autotools "flags are
# global" arrangement. All are imported targets.
target_link_libraries(ltfs PUBLIC
    PkgConfig::FUSE
    LibXml2::LibXml2
    PkgConfig::UUID
    ICU::uc ICU::i18n ICU::io ICU::data
    Threads::Threads
    ${CMAKE_DL_LIBS})
if(RT_LIBRARY)
    target_link_libraries(ltfs PUBLIC ${RT_LIBRARY})
endif()
if(LTFS_ENABLE_SNMP)
    target_link_libraries(ltfs PUBLIC PkgConfig::NETSNMP)
endif()

ltfs_link_message(ltfs libltfs)
ltfs_link_message(ltfs internal_error)
ltfs_link_message(ltfs tape_common)

install(TARGETS ltfs LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Public headers (the autotools nobase_pkginclude_HEADERS set), preserving the
# libltfs/ and tape_drivers/ subpaths under <includedir>/ltfs.
set(_pub_hdrs
    libltfs/config_file.h libltfs/dcache_ops.h libltfs/iosched_ops.h
    libltfs/kmi_ops.h libltfs/ltfs_fsops.h libltfs/tape_ops.h
    libltfs/ltfs_error.h libltfs/ltfs.h libltfs/ltfslogging.h
    libltfs/ltfstrace.h libltfs/ltfs_types.h libltfs/ltfs_thread.h
    libltfs/ltfs_locking.h libltfs/ltfs_locking_old.h libltfs/ltfs_locking_new.h
    libltfs/queue.h libltfs/uthash.h libltfs/uthash_ext.h libltfs/plugin.h
    libltfs/tape.h libltfs/xml.h
    libltfs/arch/signal_internal.h libltfs/arch/arch_info.h
    libltfs/arch/time_internal.h libltfs/arch/errormap.h
    libltfs/arch/ltfs_arch_ops.h
    tape_drivers/ibm_tape.h tape_drivers/spc_op_codes.h
    tape_drivers/ssc_op_codes.h tape_drivers/tape_drivers.h)
foreach(_h ${_pub_hdrs})
    get_filename_component(_subdir "${_h}" DIRECTORY)
    install(FILES "${CMAKE_SOURCE_DIR}/src/${_h}"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ltfs/${_subdir}")
endforeach()

# SNMP MIB definitions.
if(LTFS_ENABLE_SNMP)
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/LtfsSnmpTrapDef.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/LTFS-MIB.txt"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/share/snmp")
endif()
