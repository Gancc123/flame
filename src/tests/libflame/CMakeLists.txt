set(TESTS_LIBCHUNK_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/tests/libflame)

# gateway==libflame test

add_executable(gateway_client_test_cc
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${libchunk_objs}
    ${libflame_objs}
    ${memory_objs}
    ../../service/libflame_client.cc
    gateway_client_test.cc
    )
target_link_libraries(gateway_client_test_cc 
            common
            ${flame_grpc_deps}
            ${SPDK_LIBRARIES}
            )

add_custom_command(
    TARGET gateway_client_test_cc POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
            ${TESTS_LIBCHUNK_OUTPUT_DIR})

set_target_properties(gateway_client_test_cc
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )

# c version test
add_executable(gateway_client_test_c
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${libchunk_objs}
    ${libflame_objs}
    ${memory_objs}
    ../../service/libflame_client.cc
    gateway_client_test.c
    ../../libflame/libflame_api.cc
    )
target_link_libraries(gateway_client_test_c 
            common
            ${flame_grpc_deps}
            ${SPDK_LIBRARIES}
            )

set_target_properties(gateway_client_test_c
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )


