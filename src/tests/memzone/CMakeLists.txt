set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/tests/memzone")

add_executable(allocator_test
    allocator_test.cc
    ${memory_objs}
    )

target_link_libraries(allocator_test 
    common
    ${CMAKE_DL_LIBS}
    ${SPDK_LIBRARIES}
    )

set_target_properties(allocator_test 
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

add_custom_command(
    TARGET allocator_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_mgr.cfg
            ${OUTPUT_DIR}
    )
