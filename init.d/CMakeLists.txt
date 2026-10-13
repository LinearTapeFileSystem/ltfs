# Service script, installed only when absent (it may carry local edits).
install(CODE "
    if(NOT EXISTS \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_DATADIR}/ltfs/ltfs\")
        file(INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/ltfs\"
             DESTINATION \"${CMAKE_INSTALL_FULL_DATADIR}/ltfs\"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                              GROUP_READ GROUP_EXECUTE
                              WORLD_READ WORLD_EXECUTE)
    endif()")
