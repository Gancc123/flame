#include "include/libflame.h"

#include "proto/libflame.grpc.pb.h"
#include "proto/libflame.pb.h"
#include "libflame/log_libflame.h"
#include "libflame/libchunk/libchunk.h"
#include "include/csdc.h"
#include "util/ip_op.h"

#include <grpcpp/grpcpp.h>
#include <regex>
#include <string>

#define CONCURRENCY_MAX 1000
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#if __GNUC__ >= 4
    #define FLAME_API __attribute__((visibility ("default")))
#else
    #define FLAME_API
#endif
namespace flame {

int sub[CONCURRENCY_MAX] = {};
int sub_index = 0;


struct cb_arg{
    int* sub;
    int total_completion;
    libflame_callback cb;
    void* arg;
};


void sub_cb(const Response& res, void* arg1){
    struct cb_arg* arg= (struct cb_arg *)arg1;
    (*(arg->sub))++;
    if(*(arg->sub) == arg->total_completion){
        *(arg->sub) = 0;
        if(arg->cb != nullptr)
            arg->cb(res, arg->arg);
    }

}
//Libflame write
int FlameStub::read(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(len % 4096 != 0) return -1;
    volume->read(cmd_client_stub, buff, offset, len, cb, arg);
    return 0;
}
//Libflame write
int FlameStub::write(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(len % 4096 != 0) return -1;
    volume->write(cmd_client_stub, buff, offset, len, cb, arg);
    return 0;
}
//Libflame reset
int FlameStub::reset(uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    volume->reset(cmd_client_stub, offset, len, cb, arg);
    return 0;
}
//Libflame flush
int FlameStub::flush(libflame_callback cb, void* arg){
    volume->flush(cmd_client_stub, cb, arg);
    return 0;
}

//Class Volume
int Volume::vol_to_chunks(uint64_t offset, uint64_t length, std::vector<ChunkOffLen>& chunk_positions){
    uint64_t chunk_size = volume_meta_.chk_sz;
    int start_index = offset / chunk_size;
    int end_index = (offset + length) / chunk_size + ( ((offset + length) % chunk_size) == 0 ? (-1) : 0);
    uint64_t offset_inchunk, length_inchunk;
    offset_inchunk = offset - start_index * chunk_size;
    length_inchunk = length > chunk_size - offset_inchunk ? chunk_size - offset_inchunk : length;
    ChunkOffLen chunk_position;
    chunk_position.chunk_id = volume_meta_.chunks_map[start_index].chunk_id;
    chunk_position.offset = offset_inchunk;
    chunk_position.length = length_inchunk;
    chunk_positions.push_back(chunk_position);
    if(start_index == end_index){
        return 0;
    }
    for(int i = start_index + 1; i < end_index; i++){
        chunk_position.chunk_id = volume_meta_.chunks_map[i].chunk_id;
        chunk_position.offset = 0;
        chunk_position.length = chunk_size;
        chunk_positions.push_back(chunk_position);
    }
    chunk_position.chunk_id = volume_meta_.chunks_map[end_index].chunk_id;
    chunk_position.offset = 0;
    chunk_position.length = (offset + length) - end_index * chunk_size;
    chunk_positions.push_back(chunk_position);
    return 0;
}
// async io call
int Volume::read(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(buff.size() < len) return -1;
    std::vector<ChunkOffLen> chunk_positions;
    vol_to_chunks(offset, len, chunk_positions);
    uint64_t addr = (uint64_t)buff.addr();
    uint32_t rkey = buff.rkey();
    sub_index = sub_index++ % CONCURRENCY_MAX;
    cb_arg* sub_arg = new cb_arg {&sub[sub_index], (int)chunk_positions.size(), cb, arg}; 

    for(int i = 0; i < chunk_positions.size(); i++){  //暂时都采用无inline的读
        uint64_t peer_io_op = volume_meta_.chunks_map[i].ip;
        uint64_t peer_io_addr = peer_io_op << 16 | volume_meta_.chunks_map[i].port;
        uint64_t chunk_id = chunk_positions[i].chunk_id;
        uint64_t offset_inner = chunk_positions[i].offset;
        uint64_t length_inner = chunk_positions[i].length;
        if(length_inner < 4096){
            std::cout << "not a block request" << std::endl;
            return -1;
        } 
        RdmaWorkRequest* rdma_work_request_read = cmd_client_stub->get_request();
        MemoryArea* memory_read = new MemoryAreaImpl(addr, length_inner, rkey, 1);
        cmd_t* cmd_read = (cmd_t *)rdma_work_request_read->command;
        ChunkReadCmd* read_cmd = new ChunkReadCmd(cmd_read, chunk_id, offset_inner, length_inner, *memory_read); 
        cmd_client_stub->submit(*rdma_work_request_read, peer_io_addr, sub_cb, (void*)sub_arg);
        addr += length_inner;
    }
    return 0;
}
int Volume::write(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(buff.size() < len) return -1;
    std::vector<ChunkOffLen> chunk_positions;
    vol_to_chunks(offset, len, chunk_positions);
    uint64_t addr = (uint64_t)buff.addr();
    uint32_t rkey = buff.rkey();
    sub_index = sub_index++ % CONCURRENCY_MAX;
    cb_arg* sub_arg = new cb_arg {&sub[sub_index], (int)chunk_positions.size(), cb, arg};

    for(int i = 0; i < chunk_positions.size(); i++){  //暂时全部都采用无inline的写
        uint64_t peer_io_op = volume_meta_.chunks_map[i].ip;
        uint64_t peer_io_addr = peer_io_op << 16 | (uint32_t)volume_meta_.chunks_map[i].port;
        uint64_t chunk_id = chunk_positions[i].chunk_id;
        uint64_t offset_inner = chunk_positions[i].offset;
        uint64_t length_inner = chunk_positions[i].length;
        if(length_inner < 4096){
            std::cout << "not a block request" << std::endl;
            return -1;
        } 
        RdmaWorkRequest* rdma_work_request_write = cmd_client_stub->get_request();
        MemoryArea* memory_write = new MemoryAreaImpl(addr, length_inner, rkey, 1);
        cmd_t* cmd_write = (cmd_t *)rdma_work_request_write->command;
        ChunkWriteCmd* write_cmd = new ChunkWriteCmd(cmd_write, chunk_id, offset_inner, length_inner, *memory_write, 1); 
        cmd_client_stub->submit(*rdma_work_request_write, peer_io_addr, sub_cb, (void*)sub_arg);
        addr += length_inner;
    }
    return 0;
}
int Volume::reset(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){  
    return 0;
}
int Volume::flush(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, libflame_callback cb, void* arg){
    return 0;
}


} // namespace flame