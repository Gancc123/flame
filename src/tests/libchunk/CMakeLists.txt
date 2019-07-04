set(TESTS_LIBCHUNK_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/tests/libchunk)

# libchunk test

add_executable(server_test 
    ${chunkstore_objs}
    ${work_objs}
    ${msg_objs}
    ${memory_objs}
    ${libchunk_objs}
    server_test.cc
    )

target_link_libraries(server_test common)

add_custom_command(
    TARGET server_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_mgr.cfg
            ${TESTS_LIBCHUNK_OUTPUT_DIR})

set_target_properties(server_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )

add_executable(client_test
    ${libchunk_objs}
    ${memory_objs}
    client_test.cc
    )
target_link_libraries(client_test common)

add_custom_command(
    TARGET client_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
            ${TESTS_LIBCHUNK_OUTPUT_DIR})

set_target_properties(client_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )

