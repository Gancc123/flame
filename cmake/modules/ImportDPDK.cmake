function(import_dpdk dpdk_dir)
    set(DPDK_INCLUDE_DIR ${dpdk_dir}/build/include)
    if(EXISTS "${dpdk_dir}/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}rte_mempool${CMAKE_STATIC_LIBRARY_SUFFIX}")
        foreach(c
                bus_pci
                eal
                kvargs
                mbuf
                mempool
                mempool_ring
                pci
                ring
                )
            add_library(dpdk::${c} STATIC IMPORTED)
            set(dpdk_${c}_LIBRARY
                "${dpdk_dir}/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}rte_${c}${CMAKE_STATIC_LIBRARY_SUFFIX}")
            set_target_properties(dpdk::${c} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${DPDK_INCLUDE_DIR}
                IMPORTED_LOCATION "${dpdk_${c}_LIBRARY}")
            list(APPEND DPDK_LIBRARIES dpdk::${c})
            list(APPEND DPDK_ARCHIVES "${dpdk_${c}_LIBRARY}")
        endforeach()
    else ()
        foreach(c
                bus_pci
                eal
                kvargs
                mbuf
                mempool
                mempool_ring
                pci
                ring
                )
            add_library(dpdk::${c} SHARED IMPORTED)
            set(dpdk_${c}_LIBRARY
                "${dpdk_dir}/build/lib/${CMAKE_SHARED_LIBRARY_PREFIX}rte_${c}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set_target_properties(dpdk::${c} PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES ${DPDK_INCLUDE_DIR}
                IMPORTED_LOCATION "${dpdk_${c}_LIBRARY}")
            list(APPEND DPDK_LIBRARIES dpdk::${c})
            list(APPEND DPDK_ARCHIVES "${dpdk_${c}_LIBRARY}")
        endforeach()
    endif ()

    add_library(dpdk::dpdk INTERFACE IMPORTED)
    add_dependencies(dpdk::dpdk ${DPDK_LIBRARIES})

    set_target_properties(dpdk::dpdk PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES ${DPDK_INCLUDE_DIR}
        INTERFACE_LINK_LIBRARIES
        "-Wl,--whole-archive $<JOIN:${DPDK_ARCHIVES}, > -Wl,--no-whole-archive")
        
endfunction()