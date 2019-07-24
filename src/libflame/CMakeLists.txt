# set(LIBFLAME_DIR ${CMAKE_BINARY_DIR}/bin/libflame)
# set(LIBCHUNK_DIR ${LIBFLAME_DIR}/libchunk)

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
        ../service/libflame_client.cc
        libflame_api.cc
    )
target_link_libraries(flame
        PRIVATE ${flame_common_deps} ${ALLOC_LIBS} ${flame_grpc_deps} common
    )

# set_target_properties(flame PROPERTIES output_name "flame")

install(TARGETS flame DESTINATION /usr/lib)
install(FILES
    libflame/libflame_api.h
    DESTINATION /usr/include/fbd)