set(OUTPUT_DIR "${TESTS_MSG_OUTPUT_DIR}/msg")

add_executable(flame_test_msg_client
    test_msg_client.cc
    )

target_link_libraries(flame_test_msg_client common)

add_custom_command(
    TARGET flame_test_msg_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
            ${OUTPUT_DIR})

set_target_properties(flame_test_msg_client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )


add_executable(flame_test_msg_manager
    test_msg_manager.cc
    )

target_link_libraries(flame_test_msg_manager common)

add_custom_command(
    TARGET flame_test_msg_manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_mgr.cfg
            ${OUTPUT_DIR})

set_target_properties(flame_test_msg_manager
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )







