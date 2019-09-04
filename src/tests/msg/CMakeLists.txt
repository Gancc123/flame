set(TESTS_MSG_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/tests/msg)

add_subdirectory(rdma_conn_v2)

package_add_test(buddy_allocator_ut buddy_allocator_ut.cc)

target_link_libraries(buddy_allocator_ut common)

set_target_properties(buddy_allocator_ut
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_MSG_OUTPUT_DIR}
    )