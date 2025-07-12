add_library(iosched-fcfs SHARED fcfs.c)
target_link_libraries(iosched-fcfs
	PkgConfig::FUSE
	ltfs
	libiosched_fcfs
)
target_include_directories(iosched-fcfs BEFORE PUBLIC "..")

add_library(iosched-unified SHARED
	unified.c
	cache_manager.c
)
target_link_libraries(iosched-unified
	PkgConfig::FUSE
	ltfs
	libiosched_unified
)
target_include_directories(iosched-unified BEFORE PUBLIC "..")

set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
