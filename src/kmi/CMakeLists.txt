add_library(kmi-simple SHARED
	simple.c
	key_format_ltfs.c
)
target_link_libraries(kmi-simple
	PkgConfig::FUSE
	ltfs
	libkmi_simple
)
target_include_directories(kmi-simple BEFORE PUBLIC "..")

add_library(kmi-flatfile SHARED
	flatfile.c
	key_format_ltfs.c
)
target_link_libraries(kmi-flatfile
	PkgConfig::FUSE
	ltfs
	libkmi_flatfile
)
target_include_directories(kmi-flatfile BEFORE PUBLIC "..")

set(PLUGINS_PATHS "${PLUGINS_PATHS};${CMAKE_CURRENT_BINARY_DIR}" PARENT_SCOPE)
