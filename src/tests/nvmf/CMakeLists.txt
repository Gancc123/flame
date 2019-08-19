set(TESTS_LIBCHUNK_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/tests/nvmf_tgt)

add_executable(nvmf_tgt
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${libchunk_objs}
    ${libflame_objs}
    ${memory_objs}
    ${libflame_api_objs}
    ../../service/libflame_client.cc
    nvmf_tgt.c
    )
target_link_libraries(nvmf_tgt 
            common-except-spdk
            ${flame_grpc_deps}
            ${SPDK_FLAME_LIBRARIES}
            )

set_target_properties(nvmf_tgt
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${TESTS_LIBCHUNK_OUTPUT_DIR}
    )


