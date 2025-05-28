set(LIBTAPE_REED_SOLOMON_CRC_SRCS
  reed_solomon_crc.c
)
add_library(libtape_reed_solomon_crc SHARED ${LIBTAPE_REED_SOLOMON_CRC_SRCS})
target_link_libraries(libtape_reed_solomon_crc
  PkgConfig::FUSE
  libltfs
)

set(LIBTAPE_CRC32C_CRC_SRCS
  crc32c_crc.c
)
add_library(libtape_crc32c_crc SHARED ${LIBTAPE_CRC32C_CRC_SRCS})
target_link_libraries(libtape_crc32c_crc
  PkgConfig::FUSE
  libltfs
)
