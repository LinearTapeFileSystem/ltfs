add_library(tape-file SHARED
	filedebug_tc.c
	filedebug_conf_tc.c
	../../ibm_tape.c
)
target_link_libraries(tape-file
	PkgConfig::FUSE
	ltfs
	libtape_generic_file
)
target_include_directories(tape-file BEFORE PUBLIC "../..")
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
