set(OUTPUT_DIR "${TESTS_MSG_OUTPUT_DIR}/rdma_conn_v2")

add_executable(rdma_v2_client
    rdma_client.cc
    msger.cc
    ${memory_objs}
    )
target_link_libraries(rdma_v2_client common)

add_custom_command(
    TARGET rdma_v2_client POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
            ${OUTPUT_DIR})

set_target_properties(rdma_v2_client
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )

add_executable(rdma_v2_mgr
    rdma_mgr.cc
    msger.cc
    ${memory_objs}
    )
target_link_libraries(rdma_v2_mgr common)

add_custom_command(
    TARGET rdma_v2_mgr POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/flame_mgr.cfg
            ${OUTPUT_DIR})

set_target_properties(rdma_v2_mgr
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )


    # ------------------

# add_executable(rdma_v2_client_write
#     rdma_write_c.cc
#     msger_write.cc
#     )
# target_link_libraries(rdma_v2_client_write common)

# add_custom_command(
#     TARGET rdma_v2_client_write POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#             ${CMAKE_CURRENT_SOURCE_DIR}/flame_client.cfg
#             ${OUTPUT_DIR})

# set_target_properties(rdma_v2_client_write
#     PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
#     )

# add_executable(rdma_v2_mgr_write
#     rdma_write_s.cc
#     msger_write.cc
#     )
# target_link_libraries(rdma_v2_mgr_write common)

# add_custom_command(
#     TARGET rdma_v2_mgr_write POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#             ${CMAKE_CURRENT_SOURCE_DIR}/flame_mgr.cfg
#             ${OUTPUT_DIR})

# set_target_properties(rdma_v2_mgr_write
#     PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
#     )




