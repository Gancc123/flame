macro(package_add_test TESTNAME)
    add_executable(${TESTNAME} ${ARGN})
    target_link_libraries(${TESTNAME} gtest gmock gtest_main)
    add_test(${TESTNAME} ${TESTNAME})
    set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
endmacro()

add_subdirectory(msg)
add_subdirectory(libchunk)
# add chunkstore test
add_subdirectory(chunkstore)
add_subdirectory(memzone)
add_subdirectory(libflame)

