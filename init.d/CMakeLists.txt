# Service script, installed only when absent (it may carry local edits).
install(CODE "
    set(_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/ltfs\")
    if(NOT EXISTS \"\${_dir}/ltfs\")
        file(INSTALL \"${CMAKE_CURRENT_SOURCE_DIR}/ltfs\"
             DESTINATION \"\${_dir}\"
             FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                              GROUP_READ GROUP_EXECUTE
                              WORLD_READ WORLD_EXECUTE)
    endif()")
