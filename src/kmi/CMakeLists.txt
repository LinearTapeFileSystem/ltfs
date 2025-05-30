set(LIBKMI_SIMPLE_SRCS
  simple.c
  key_format_ltfs.c
)
add_library(libkmi_simple SHARED ${LIBKMI_SIMPLE_SRCS})
target_link_libraries(libkmi_simple
  PkgConfig::FUSE
  libltfs
  libkmi_simple_messages
)
target_include_directories(libkmi_simple BEFORE PUBLIC "..")

set(LIBKMI_FLATFILE_SRCS
  flatfile.c
  key_format_ltfs.c
)
add_library(libkmi_flatfile SHARED ${LIBKMI_FLATFILE_SRCS})
target_link_libraries(libkmi_flatfile
  PkgConfig::FUSE
  libltfs
  libkmi_flatfile_messages
)
target_include_directories(libkmi_flatfile BEFORE PUBLIC "..")
