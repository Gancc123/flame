set(TESTS_LIBFLAME_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/gtest/libflame)
set(GTEST_DIR ${CMAKE_SOURCE_DIR}/grpc)
# libflame gtest

link_directories(/usr/local/lib)
add_executable(libflame_test 
    ${memory_objs}
    ${libchunk_objs}
    ${libflame_objs}
    ${proto_objs}
    gtest_libflame.cc
    ../../service/libflame_client.cc
    )

target_link_libraries(libflame_test 
    common 
    ${flame_grpc_deps}
    libgtest.so
    gtest
    )

set_target_properties(libflame_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBFLAME_OUTPUT_DIR}
    )