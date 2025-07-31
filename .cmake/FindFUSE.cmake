####
# Find libfuse and set the offset to 64bits
###
find_package(PkgConfig REQUIRED)

pkg_check_modules(FUSE REQUIRED "fuse>=2.6.0" IMPORTED_TARGET)
target_compile_definitions(PkgConfig::FUSE INTERFACE
	_FILE_OFFSET_BITS=64
)
