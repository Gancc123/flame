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

int sub[CONCURRENCY_MAX];
int sub_index;

struct cb_arg{
    int& sub;
    int total_completion;
    libflame_callback cb;
    void* arg;
};

void sub_cb(const Response& res, void* arg1){
    cb_arg arg= *(struct cb_arg *)arg1;
    arg.sub++;
    if(arg.sub == arg.total_completion)
        arg.cb(res, arg.arg);
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
int Volume::read(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(buff.size() < len) return -1;
    std::vector<ChunkOffLen> chunk_positions;
    vol_to_chunks(offset, len, chunk_positions);
    
    sub_index = (sub_index + 1) % CONCURRENCY_MAX;
    cb_arg sub_arg = {sub[sub_index],  chunk_positions.size(), cb, arg}; 

    uint64_t addr = (uint64_t)buff.addr();
    uint32_t size = 0;
    uint32_t rkey = buff.rkey();
    for(auto i : chunk_positions){  //暂时都采用无inline的读
        std::shared_ptr<CmdClientStubImpl> cmd_client_stub = CmdClientStubImpl::create_stub(ip32_to_string(volume_meta_.chunks_map[i.chunk_id].ip), volume_meta_.chunks_map[i.chunk_id].port, nullptr); //这里应该设置一个全局的连接池，现在没设计
        RdmaWorkRequest* rdma_work_request_read = cmd_client_stub->get_request();
        size = i.length;
        MemoryArea* memory_read = new MemoryAreaImpl(addr, size, rkey, 1);
        cmd_t* cmd_read = (cmd_t *)rdma_work_request_read->command;
        ChunkReadCmd* read_cmd = new ChunkReadCmd(cmd_read, i.chunk_id, i.offset, size, *memory_read); 
        cmd_client_stub->submit(*rdma_work_request_read, sub_cb, (void*)&sub_arg);
        addr += size;
    }
    return 0;
}
int Volume::write(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg){
    if(buff.size() < len) return -1;
    std::vector<ChunkOffLen> chunk_positions;
    vol_to_chunks(offset, len, chunk_positions);
    uint64_t addr = (uint64_t)buff.addr();
    uint32_t size = 0;
    uint32_t rkey = buff.rkey();

    sub_index = (sub_index + 1) % CONCURRENCY_MAX;
    cb_arg sub_arg = {sub[sub_index],  chunk_positions.size(), cb, arg};

    for(auto i : chunk_positions){  //暂时全部都采用无inline的写
        std::shared_ptr<CmdClientStubImpl> cmd_client_stub = CmdClientStubImpl::create_stub(ip32_to_string(volume_meta_.chunks_map[i.chunk_id].ip), volume_meta_.chunks_map[i.chunk_id].port, nullptr); //这里应该设置一个全局的连接池，现在没设计
        RdmaWorkRequest* rdma_work_request_write = cmd_client_stub->get_request();
        size = i.length;
        MemoryArea* memory_write = new MemoryAreaImpl(addr, size, rkey, 1);
        cmd_t* cmd_read = (cmd_t *)rdma_work_request_write->command;
        ChunkReadCmd* read_cmd = new ChunkReadCmd(cmd_read, i.chunk_id, i.offset, size, *memory_write); 
        cmd_client_stub->submit(*rdma_work_request_write, sub_cb, (void*)&sub_arg);
        addr += size;
    }
    return 0;
}
int Volume::reset(uint64_t offset, uint64_t len, libflame_callback cb, void* arg){  

}
int Volume::flush(libflame_callback cb, void* arg){

}


} // namespace flame