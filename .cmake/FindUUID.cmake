###
# Find UUID package
#
find_package(PkgConfig REQUIRED)

if(APPLE)
	pkg_check_modules(UUID REQUIRED "uuid>=1.6" IMPORTED_TARGET)
else()
	pkg_check_modules(UUID REQUIRED "uuid>=1.36" IMPORTED_TARGET)
endif()
