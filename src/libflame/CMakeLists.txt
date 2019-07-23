# set(LIBFLAME_DIR ${CMAKE_BINARY_DIR}/bin/libflame)
# set(LIBCHUNK_DIR ${LIBFLAME_DIR}/libchunk)

# add_subdirectory(libchunk)

set(CMAKE_C_FLAGS "") 
add_library(flame SHARED libflame_api.cc)

# set_target_properties(flame PROPERTIES output_name "flame")

install(TARGETS flame DESTINATION /usr/lib)
install(FILES
    libflame/libflame_api.h
    DESTINATION /usr/include/fbd)