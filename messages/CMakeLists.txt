# file(GLOB _MESSAGES_LIBS RELATIVE "." "*/")
set(MESSAGES_LIBS
  bin_ltfs
  bin_ltfsck
  bin_ltfsindextool
  bin_mkltfs
  internal_error
  iosched_fcfs
  iosched_unified
  kmi_flatfile
  kmi_simple
  libltfs
  tape_common
  tape_freebsd_cam
  tape_generic_file
  tape_generic_itdtimg
  tape_iokit
  tape_linux_lin_tape
  tape_linux_sg
)

foreach(NAME IN LISTS MESSAGES_LIBS)
  file(GLOB SRCS "${NAME}/*.txt")

  add_custom_command(
    OUTPUT "res_${NAME}/lib${NAME}.a"
    # ALL
    COMMAND mkdir -p res_${NAME}
    COMMAND ${ICU_GENRB_EXECUTABLE} -q ${SRCS} -d res_${NAME} 1> /dev/null
    COMMAND find res_${NAME} -name *.res > res_${NAME}/paths.txt
    COMMAND ${ICU_PKGDATA_EXECUTABLE} -d res_${NAME}/ -m static -p ${NAME} -q res_${NAME}/paths.txt 1> /dev/null
    BYPRODUCTS "res_${NAME}"
    # SOURCES
  )
  add_custom_target("${NAME}.a" ALL
    cp res_${NAME}/lib${NAME}.a lib${NAME}.a
    DEPENDS "res_${NAME}/lib${NAME}.a"
    BYPRODUCTS lib${NAME}.a
  )

  add_library("lib${NAME}_messages" STATIC IMPORTED GLOBAL)
  set_target_properties("lib${NAME}_messages" PROPERTIES IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/lib${NAME}.a")
endforeach()
