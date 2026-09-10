# Integration tests: the shell scripts under t/ (introduced by the test-suite
# change) run through ctest. Register whatever is present so this build works
# with or without the suite; SKIP_RETURN_CODE 77 covers hosts without
# /dev/fuse.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/helpers/fsops_helper.c")
    add_executable(fsops_helper helpers/fsops_helper.c)
    set_target_properties(fsops_helper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests/helpers")
endif()

file(GLOB _test_scripts "${CMAKE_CURRENT_SOURCE_DIR}/t/*.sh")
foreach(_path ${_test_scripts})
    get_filename_component(_t "${_path}" NAME_WE)
    add_test(NAME ${_t} COMMAND sh "${_path}")
    set_tests_properties(${_t} PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT "top_srcdir=${CMAKE_SOURCE_DIR};top_builddir=${CMAKE_BINARY_DIR}")
endforeach()
