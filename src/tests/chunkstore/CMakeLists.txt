set(OUTPUT_DIR "${CMAKE_BINARY_DIR}/bin/chunkstore")

add_executable(chunkstore_test
    chunkstore_test.cc
    ${chunkstore_objs}
    )

target_link_libraries(chunkstore_test
    common
    ${CMAKE_DL_LIBS}
    ${SPDK_LIBRARIES}
    )

set_target_properties(chunkstore_test
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR}
    )
