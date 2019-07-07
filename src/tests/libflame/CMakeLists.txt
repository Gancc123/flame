set(TESTS_LIBCHUNK_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/tests/libflame)

# gateway==libflame test

add_executable(gateway_client_test
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${libchunk_objs}
    ${memory_objs}
    ../../service/libflame_client.cc
    gateway_client_test.cc
    ../../libflame/libflame.cc
    )
target_link_libraries(gateway_client_test 
            common
            ${flame_grpc_deps}
            ${SPDK_LIBRARIES}
            )

add_custom_command(
    TARGET gateway_client_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
            ${TESTS_LIBCHUNK_OUTPUT_DIR})

set_target_properties(gateway_client_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )

