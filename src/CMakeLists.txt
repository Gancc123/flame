set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-format-security -Wno-return-type")

# requeired by spdlog
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pthread")

if(CPU_PROFILE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} \
-Wl,--no-as-needed -lprofiler -Wl,--as-needed" )
endif(CPU_PROFILE)

# sort out which allocator to use
if(ALLOCATOR STREQUAL "tcmalloc")
    set(ALLOC_LIBS ${GPERFTOOLS_TCMALLOC_LIBRARY})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-malloc \
-fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free")
    set(TCMALLOC_srcs perfglue/heap_profiler.cc)
elseif(ALLOCATOR STREQUAL "tcmalloc_minimal")
    set(ALLOC_LIBS ${GPERFTOOLS_TCMALLOC_MINIMAL_LIBRARY})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-malloc \
-fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free")
    set(TCMALLOC_srcs perfglue/disabled_heap_profiler.cc)
elseif(ALLOCATOR STREQUAL "libc")
    set(TCMALLOC_srcs perfglue/disabled_heap_profiler.cc)
else()
    set(TCMALLOC_srcs perfglue/disabled_heap_profiler.cc)
    message(FATAL_ERROR "Unsupported allocator selected: ${ALLOCATOR}")
endif()

if(HAVE_RDMA)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -I${RDMA_INCLUDE_DIR}")
  list(APPEND EXTRALIBS ${RDMA_LIBRARIES})
endif(HAVE_RDMA)

include(TestBigEndian)
test_big_endian(FLAME_BIG_ENDIAN)
if(NOT FLAME_BIG_ENDIAN)
    set(FLAME_LITTLE_ENDIAN 1)
endif()

if(LINUX)
    add_definitions("-D_GNU_SOURCE")
endif()

# Now create a useable config.h
configure_file(
    ${CMAKE_SOURCE_DIR}/src/include/acconfig-h.in.cmake
    ${CMAKE_BINARY_DIR}/include/acconfig.h
)

set(flame_common_deps
    ${CMAKE_THREAD_LIBS_INIT}
    ${Boost_LIBRARIES}
    )
if(HAVE_RDMA)
    list(APPEND flame_common_deps ${RDMA_LIBRARY})
endif(HAVE_RDMA)


#### util
add_library(util-objs OBJECT
    util/str_util.cc
    util/fs_util.cc
    util/option_parser.cc
    )
list(APPEND obj_modules util)


#### common
add_library(common-objs OBJECT
    common/thread/io_priority.cc
    common/thread/signal.cc
    common/thread/mutex.cc
    common/thread/thread.cc
    common/log.cc
    common/config.cc
    common/context.cc
    common/cmdline.cc
    common/convert.cc
    )
list(APPEND obj_modules common)


#### proto
set(proto_srcs
    ${PROJECT_SOURCE_DIR}/proto/csds.proto
    ${PROJECT_SOURCE_DIR}/proto/flame.proto
    ${PROJECT_SOURCE_DIR}/proto/gateway.proto
    ${PROJECT_SOURCE_DIR}/proto/internal.proto
    ${PROJECT_SOURCE_DIR}/proto/libflame.proto
    )

#Generate gprc proto files
PROTOBUF_GENERATE_CPP(pb_srcs  pb_hdrs  ${CMAKE_BINARY_DIR}/include/proto
     ${proto_srcs})
GRPC_GENERATE_CPP(grpc_pb_srcs  grpc_pb_hdrs   ${CMAKE_BINARY_DIR}/include/proto 
     ${proto_srcs})

add_custom_target(x_pb_tgt
    SOURCES ${pb_hdrs} ${pb_srcs} ${grpc_pb_hdrs} ${grpc_pb_srcs}
    )

add_library(proto-objs OBJECT
    ${pb_srcs} 
    ${grpc_pb_srcs}
    )

list(APPEND obj_modules proto)

#### service
add_library(service-objs OBJECT
    service/flame_service.cc
    service/flame_client.cc
    service/libflame_service.cc
    service/libflame_client.cc
    service/internal_service.cc
    service/internal_client.cc
    service/csds_service.cc
    service/csds_client.cc
    )

list(APPEND obj_modules service)


#### cml
add_library(cml-objs OBJECT
    cml/flame.cc
    )
list(APPEND obj_modules cml)


#### csm
add_library(csm-objs OBJECT
    csm/csm.cc
    )
list(APPEND obj_modules csm)


#### orm
add_library(orm-objs OBJECT
    orm/stmt.cc
    orm/my_impl/my_impl.cc
    orm/engine.cc
    )
list(APPEND obj_modules orm)


#### metastore
add_library(metastore-objs OBJECT
    metastore/sqlms/sqlms.cc
    metastore/ms.cc
    )
list(APPEND obj_modules metastore)


#### chunkstore
set(nvmestore_srcs)
if(WITH_SPDK)
    set(nvmestore_srcs
        chunkstore/nvmestore/chunkblobmap.cc
        chunkstore/nvmestore/iochannels.cc
        chunkstore/nvmestore/nvmechunkmap.cc
        chunkstore/nvmestore/nvmechunk.cc
        chunkstore/nvmestore/nvmeconf.cc
        chunkstore/nvmestore/nvmestore.cc
        )
endif(WITH_SPDK)

add_library(chunkstore-objs OBJECT
    chunkstore/simstore/simstore.cc
    chunkstore/filestore/filestore.cc
    chunkstore/filestore/filechunk.cc
    chunkstore/filestore/filechunkmap.cc
    chunkstore/filestore/filestoreconf.cc
    chunkstore/filestore/chunkutil.cc
    chunkstore/filestore/object.cc
    chunkstore/cs.cc
    ${nvmestore_srcs}
    )
list(APPEND obj_modules chunkstore)


#### memory
add_library(memory-objs OBJECT
    memzone/rdma/BuddyAllocator.cc
    memzone/rdma/memory_conf.cc
    memzone/rdma/RdmaMem.cc
    memzone/rdma_mz.cc
    )
list(APPEND obj_modules memory)

#### mgr
add_library(mgr-objs OBJECT
    mgr/mgr.cc
    mgr/mgr_server.cc
    mgr/chkm/chk_mgmt.cc
    mgr/csdm/csd_mgmt.cc
    mgr/volm/vol_mgmt.cc
    )
list(APPEND obj_modules mgr)


#### msg
set(msg_common_srcs
    msg/internal/errno.cc
    msg/internal/node_addr.cc
    msg/internal/msg_config.cc
    msg/internal/types_helper.cc
    msg/internal/util.cc
    msg/NetHandler.cc
    msg/event/EventPoller.cc
    msg/Msg.cc
    msg/MsgWorker.cc
    msg/Session.cc
    msg/MsgManager.cc
    msg/socket/TcpListenPort.cc
    msg/socket/TcpConnection.cc
    msg/socket/TcpStack.cc)

set(msg_rdma_common_srcs)
if(HAVE_RDMA)
  list(APPEND msg_rdma_common_srcs
    msg/rdma/BuddyAllocator.cc
    msg/rdma/Infiniband.cc
    msg/rdma/RdmaMem.cc
    msg/rdma/MemoryManager.cc
    msg/rdma/RdmaListenPort.cc
    msg/rdma/RdmaPrepConn.cc
    msg/rdma/RdmaConnection.cc
    msg/rdma/RdmaStack.cc)
endif(HAVE_RDMA)

set(msg_spdk_srcs)
if(HAVE_SPDK)
    list(APPEND msg_spdk_srcs
      msg/spdk/SpdkMsgWorker.cc)
endif(HAVE_SPDK)

add_library(msg-objs OBJECT
    msg/internal/errno.cc
    msg/internal/node_addr.cc
    msg/internal/msg_config.cc
    msg/internal/types_helper.cc
    msg/internal/util.cc
    msg/NetHandler.cc
    msg/event/EventPoller.cc
    msg/Msg.cc
    msg/MsgWorker.cc
    msg/Session.cc
    msg/MsgManager.cc
    msg/socket/TcpListenPort.cc
    msg/socket/TcpConnection.cc
    msg/socket/TcpStack.cc
    ${msg_common_srcs}
    ${msg_rdma_common_srcs}
    ${msg_spdk_srcs}
    msg/Stack.cc
    msg/msg_context.cc
    )
list(APPEND obj_modules msg)


#### work
add_library(work-objs OBJECT
    work/timer_work.cc
    )
list(APPEND obj_modules work)


#### cluster
add_library(cluster-objs OBJECT
    cluster/clt_my/my_mgmt.cc
    )
list(APPEND obj_modules cluster)


#### layout
add_library(layout-objs OBJECT
    layout/calculator.cc
    layout/poll_layout.cc
    )
list(APPEND obj_modules layout)


#### spolicy
add_library(spolicy-objs OBJECT
    spolicy/spolicy.cc
    )
list(APPEND obj_modules spolicy)


#### csd 
add_library(csd-objs OBJECT
    csd/csd.cc
    csd/csd_admin.cc
    )
list(APPEND obj_modules csd)


#### libchunk 
add_library(libchunk-objs OBJECT
    libflame/libchunk/libchunk.cc
    libflame/libchunk/cmd_service_mapper.cc
    libflame/libchunk/msg_handle.cc
    )
list(APPEND obj_modules libchunk)

#### libflame
add_library(libflame-objs OBJECT
    libflame/libflame.cc
    )
list(APPEND obj_modules libflame)

#### libflame_api
add_library(libflame_api-objs OBJECT
    libflame/libflame_api.cc
    )
list(APPEND obj_modules libflame_api)


#### gen xxx_objs
foreach(module IN LISTS obj_modules)
    # generate *.pb.h first of all
    add_dependencies(${module}-objs x_pb_tgt)
    set(${module}_objs $<TARGET_OBJECTS:${module}-objs>)
endforeach()


#### lib common
set(flame_common_objs
    ${util_objs}
    ${common_objs}
    ${msg_objs}
    )



# foreach(o IN LISTS flame_common_objs)
#     message(STATUS "${o}")
# endforeach()

add_library(common STATIC ${flame_common_objs})
target_link_libraries(common
  PRIVATE ${flame_common_deps} ${ALLOC_LIBS}
  PUBLIC ${SPDK_LIBRARIES})
if(WITH_STATIC_LIBSTDCXX)
    # the apps linking against libcommon are daemons also written in C++, so we
    # need to link them against libstdc++.
    target_link_libraries(common
        INTERFACE "-static-libstdc++ -static-libgcc")
endif()


add_library(flame-common SHARED ${flame_common_objs})
target_link_libraries(flame-common ${flame_common_deps} ${ALLOC_LIBS})
# appease dpkg-shlibdeps
set_target_properties(flame-common PROPERTIES
    SOVERSION 0
    INSTALL_RPATH "")
if(WITH_STATIC_LIBSTDCXX)
    # link libstdc++ into flame-common to avoid including libstdc++ in every 
    # apps, to reduce the size of the app linking against flame-common.
    set_target_properties(flame-common PROPERTIES
      LINK_FLAGS "-static-libstdc++ -static-libgcc")
endif()

set(flame_grpc_deps
    gRPC::grpc
    gRPC::grpc++
    gRPC::grpc++_reflection
    protobuf::libprotobuf
    )

#### gen mgr
add_executable(mgr 
    ${mgr_objs}
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${metastore_objs}
    ${msg_objs}
    ${orm_objs}
    ${cluster_objs}
    ${layout_objs}
    ${libchunk_objs}
    ${memory_objs}
    ${spolicy_objs}
    service/internal_service.cc
    service/csds_client.cc
    service/flame_service.cc
    service/libflame_service.cc
    )
target_link_libraries(mgr PRIVATE
    ${CMAKE_DL_LIBS}
    common 
    ${AIO_LIBS}
    ${flame_grpc_deps}
    ${SPDK_LIBRARIES}
    mysqlcppconn
    )

#### gen csd 
add_executable(csd 
    ${csd_objs}
    ${chunkstore_objs}
    ${proto_objs}
    ${work_objs}
    ${msg_objs}
    ${libchunk_objs}
    ${memory_objs}
    service/internal_client.cc
    service/csds_service.cc
    )
target_link_libraries(csd PRIVATE
    ${CMAKE_DL_LIBS}
    common 
    ${AIO_LIBS}
    ${flame_grpc_deps}
    ${SPDK_LIBRARIES}
    )

#### gen cml
add_executable(cml
    ${cml_objs}
    ${proto_objs}
    service/flame_client.cc
    )
target_link_libraries(cml PRIVATE
    common
    ${flame_grpc_deps}
    )

#### gen csm
add_executable(csm
    ${csm_objs}
    ${proto_objs}
    service/internal_client.cc
    )
target_link_libraries(csm PRIVATE
    common
    ${flame_grpc_deps}
    )

add_subdirectory(tests)

add_subdirectory(libflame)
add_subdirectory(gtest)
