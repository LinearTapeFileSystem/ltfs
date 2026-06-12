# Helper for LTFS runtime plugins (tape backends, I/O schedulers, key managers).
# Reproduces the autotools "-module -avoid-version" behavior: an unversioned
# shared module named lib<x>.so, installed under <libdir>/ltfs.

# ltfs_add_plugin(<target>
#     SOURCES <files...>
#     [MSG <message-bundle-name>]
#     [CRC]                       # link the SSE-optimized CRC objects
#     [INCLUDES <dirs...>]
#     [DEFINES <defs...>]
#     [LIBS <libs...>])
function(ltfs_add_plugin target)
    cmake_parse_arguments(P "CRC" "MSG" "SOURCES;INCLUDES;DEFINES;LIBS" ${ARGN})

    add_library(${target} MODULE ${P_SOURCES})
    set_target_properties(${target} PROPERTIES
        PREFIX ""            # name is exactly <target>.so, no extra lib prefix
        SUFFIX ".so"         # plugins are .so even on macOS (loader expects it)
        OUTPUT_NAME "${target}")

    target_link_libraries(${target} PRIVATE ltfs ${P_LIBS})
    if(P_CRC)
        target_link_libraries(${target} PRIVATE ltfs_crc)
    endif()
    if(P_INCLUDES)
        target_include_directories(${target} PRIVATE ${P_INCLUDES})
    endif()
    if(P_DEFINES)
        target_compile_definitions(${target} PRIVATE ${P_DEFINES})
    endif()
    if(P_MSG)
        ltfs_link_message(${target} ${P_MSG})
    endif()

    install(TARGETS ${target} LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/ltfs)
endfunction()
