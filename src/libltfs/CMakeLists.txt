file(GLOB_RECURSE LIBLTFS_SRCS LIST_DIRECTORIES false "*.c")
add_library(ltfs SHARED ${LIBLTFS_SRCS})
target_include_directories(ltfs BEFORE PUBLIC "..")

target_link_libraries(ltfs
	Threads::Threads
	PkgConfig::FUSE
	LibXml2::LibXml2
	PkgConfig::UUID
	libbin_ltfs
	liblibltfs
	libinternal_error
	libtape_common
)

if(ENABLE_SNMP)
	target_link_libraries(ltfs NetSNMP)
endif()
