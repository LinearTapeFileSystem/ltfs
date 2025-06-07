add_library(kmi_simple SHARED
  simple.c
  key_format_ltfs.c
)
target_link_libraries(kmi_simple
  PkgConfig::FUSE
  ltfs
  libkmi_simple
)
target_include_directories(kmi_simple BEFORE PUBLIC "..")

add_library(kmi_flatfile SHARED
  flatfile.c
  key_format_ltfs.c
)
target_link_libraries(kmi_flatfile
  PkgConfig::FUSE
  ltfs
  libkmi_flatfile
)
target_include_directories(kmi_flatfile BEFORE PUBLIC "..")
