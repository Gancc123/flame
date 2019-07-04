#Import:
#  SPDK_INCLUDE_DIR
#  SPDK_LIBRARIES

function(import_spdk spdk_dir)
    if(NOT DPDK_DIR)
        set(DPDK_DIR "${spdk_dir}/dpdk")
    endif()

    if(NOT TARGET dpdk::dpdk)
        include(ImportDPDK)
        import_dpdk(${DPDK_DIR})
    endif()

    if(EXISTS ${spdk_dir}/include/spdk/version.h)
        set(SPDK_FOUND TRUE PARENT_SCOPE)
    else()
        set(SPDK_FOUND FALSE PARENT_SCOPE)
        return()
    endif()

    set(SPDK_INCLUDE_DIR "${spdk_dir}/include")

    find_package(aio REQUIRED)
    find_package(uuid REQUIRED)
    find_package(rdma REQUIRED)
    
    foreach(c
            copy_ioat
            ioat
            bdev_malloc
            bdev_null
            bdev_nvme
            nvme
            vbdev_passthru
            vbdev_error
            vbdev_gpt
            vbdev_split
            # vbdev_raid
            bdev_aio
            bdev_virtio
            virtio
            sock
            sock_posix
            event_bdev
            event_copy
            blobfs
            blob
            bdev
            blob_bdev
            copy
            event
            thread
            util
            conf
            trace
            log
            lvol
            jsonrpc
            json
            rpc)
        add_library(spdk::${c} STATIC IMPORTED)
        set(spdk_${c}_LIBRARY
            "${spdk_dir}/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}spdk_${c}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set_target_properties(spdk::${c} PROPERTIES
            IMPORTED_LOCATION "${spdk_${c}_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${SPDK_INCLUDE_DIR}")
        list(APPEND SPDK_LIBRARIES_WHOLE_ARCHIVE "${spdk_${c}_LIBRARY}")
    endforeach()

    add_library(spdk::spdk_whole_archive INTERFACE IMPORTED)
    add_dependencies(spdk::spdk_whole_archive ${SPDK_LIBRARIES_WHOLE_ARCHIVE})

    set_target_properties(spdk::spdk_whole_archive PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${SPDK_INCLUDE_DIR})

    set_target_properties(spdk::bdev_aio PROPERTIES
        INTERFACE_LINK_LIBRARIES ${AIO_LIBS})

    list(APPEND SPDK_LIBRARIES spdk::spdk_whole_archive)

    foreach(c 
            env_dpdk)
        add_library(spdk::${c} STATIC IMPORTED)
        set_target_properties(spdk::${c} PROPERTIES
            IMPORTED_LOCATION "${spdk_dir}/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}spdk_${c}${CMAKE_STATIC_LIBRARY_SUFFIX}"
            INTERFACE_INCLUDE_DIRECTORIES ${SPDK_INCLUDE_DIR})
    endforeach()

    set_property(TARGET spdk::env_dpdk PROPERTY
        INTERFACE_LINK_LIBRARIES 
        dpdk::dpdk numa dl rt crypto
        )

    set_property(TARGET spdk::spdk_whole_archive PROPERTY
        INTERFACE_LINK_LIBRARIES 
        "-Wl,--whole-archive $<JOIN:${SPDK_LIBRARIES_WHOLE_ARCHIVE}, > -Wl,--no-whole-archive" aio uuid rdmacm ibverbs spdk::env_dpdk)

    
    set(SPDK_INCLUDE_DIR ${SPDK_INCLUDE_DIR} PARENT_SCOPE)
    set(SPDK_LIBRARIES ${SPDK_LIBRARIES} PARENT_SCOPE)

endfunction()

