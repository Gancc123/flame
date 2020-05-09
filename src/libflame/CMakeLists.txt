set(LIBFLAME_OUTPUT_DIR ${CMAKE_BINARY_DIR}/lib)

# add_subdirectory(libchunk)

set(CMAKE_C_FLAGS "") 
add_library(flame SHARED 
        ${chunkstore_objs}
        ${proto_objs}
        ${work_objs}
        ${libchunk_objs}
        ${libflame_objs}
        ${memory_objs}
        ${msg_objs}
        ${common_objs}
        ../service/libflame_client.cc
        libflame_api.cc
    )
target_link_libraries(flame
        PRIVATE common-except-spdk ${flame_grpc_deps}
    )
# target_link_libraries(flame
#         PRIVATE ${flame_common_deps} ${flame_grpc_deps} common-except-spdk
#     )
set_target_properties(flame
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${LIBFLAME_OUTPUT_DIR}
    )

add_custom_command(
    TARGET flame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            "${LIBFLAME_OUTPUT_DIR}/libflame.so"
            "/usr/lib/"
    COMMAND ${CMAKE_COMMAND} -E copy
            "${PROJECT_SOURCE_DIR}/src/include/libflame_api.h"
            "/usr/include/"
    COMMAND echo "copy libflame.so to /usr/lib/ success..."
    COMMAND ./configure --with-rdma --with-fbd WORKING_DIRECTORY ${SPDK_DIR}
    COMMAND make WORKING_DIRECTORY ${SPDK_DIR}
    COMMENT echo "spdk-modify compile success...")

# set_target_properties(flame PROPERTIES output_name "flame")

# install(TARGETS flame DESTINATION /usr/lib)
# install(FILES
#     include/libflame_api.h
#     DESTINATION /usr/include/fbd)