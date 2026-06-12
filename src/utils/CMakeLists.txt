add_executable(mkltfs mkltfs.c)
set_target_properties(mkltfs PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(mkltfs PRIVATE ltfs)
ltfs_link_message(mkltfs bin_mkltfs)
install(TARGETS mkltfs RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

add_executable(ltfsck ltfsck.c)
set_target_properties(ltfsck PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(ltfsck PRIVATE ltfs)
ltfs_link_message(ltfsck bin_ltfsck)
install(TARGETS ltfsck RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(PROGRAMS ltfs_ordered_copy DESTINATION ${CMAKE_INSTALL_BINDIR})
