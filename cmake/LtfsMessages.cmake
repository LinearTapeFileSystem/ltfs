# ICU message-bundle compilation for the CMake build. Reuses the shared
# messages/make_message_src.sh so the genrb/pkgdata logic lives in one place.

# ltfs_add_message_bundle(<name>)
#   Compiles messages/<name>/*.txt into <build>/messages/lib<name>_dat.a and
#   registers a build target msg_<name>. Link it into a consumer with
#   ltfs_link_message().
function(ltfs_add_message_bundle name)
    set(_src_dir "${CMAKE_SOURCE_DIR}/messages/${name}")
    set(_out_dir "${CMAKE_BINARY_DIR}/messages")
    set(_archive "${_out_dir}/lib${name}_dat.a")
    file(GLOB _txt "${_src_dir}/*.txt")

    add_custom_command(
        OUTPUT "${_archive}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_out_dir}"
        COMMAND ${CMAKE_COMMAND} -E env "GENRB=${GENRB}" "PKGDATA=${PKGDATA}"
                sh "${CMAKE_SOURCE_DIR}/messages/make_message_src.sh"
                "lib${name}_dat.a" "${_src_dir}" "${_out_dir}"
        DEPENDS ${_txt} "${CMAKE_SOURCE_DIR}/messages/make_message_src.sh"
        COMMENT "Compiling message bundle ${name}"
        VERBATIM)

    add_custom_target(msg_${name} DEPENDS "${_archive}")
    # Record the archive path for ltfs_link_message().
    set_property(GLOBAL PROPERTY ltfs_msg_archive_${name} "${_archive}")
endfunction()

# ltfs_link_message(<target> <name>)
#   Links message bundle <name> into <target> (as a plain archive input, the
#   way the autotools build put it on LDFLAGS) and orders the build after it.
function(ltfs_link_message target name)
    get_property(_archive GLOBAL PROPERTY ltfs_msg_archive_${name})
    if(NOT _archive)
        message(FATAL_ERROR "Unknown message bundle '${name}'")
    endif()
    target_link_libraries(${target} PRIVATE "${_archive}")
    add_dependencies(${target} msg_${name})
endfunction()
