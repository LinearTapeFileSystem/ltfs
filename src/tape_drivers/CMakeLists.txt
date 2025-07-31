add_library(tape_reed_solomon_crc SHARED
	reed_solomon_crc.c
)
target_link_libraries(tape_reed_solomon_crc
	PkgConfig::FUSE
	ltfs
)

add_library(tape_crc32c_crc SHARED
	crc32c_crc.c
)
target_link_libraries(tape_crc32c_crc
	PkgConfig::FUSE
	ltfs
)
set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
