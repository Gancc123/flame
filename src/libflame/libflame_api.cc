#include "include/libflame_api.h"

#include "common/context.h"
#include "include/libflame.h"
#include "libflame/log_libflame.h"
#include "util/ip_op.h"

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string>
#include <vector>

using std::string;
using std::vector;

namespace{

int write_log (FILE* pFile, const char *format, ...) {
    va_list arg;
    int done;

    va_start (arg, format);
    //done = vfprintf (stdout, format, arg);

    time_t time_log = time(NULL);
    struct tm* tm_log = localtime(&time_log);
    fprintf(pFile, "%04d-%02d-%02d %02d:%02d:%02d ", tm_log->tm_year + 1900, tm_log->tm_mon + 1, tm_log->tm_mday, tm_log->tm_hour, tm_log->tm_min, tm_log->tm_sec);

    done = vfprintf (pFile, format, arg);
    va_end (arg);

    fflush(pFile);
    return done;
}

/* FlameContext_API*/
extern "C" bool get_flame_context(flame_context_t* context){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    context = (void *)flame_context;
    return 0;
}

extern "C" bool flame_context_init_cfg(const char* cfg_path){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    flame_context->init_config(cfg_path);
    return 0;
}

extern "C" bool flame_context_init_log(const char* dir, const char* level, const char* prefix){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    flame_context->init_log(dir, level, prefix);
    return 0;
}
 
extern "C" const char* get_cluster_name(){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    return flame_context->cluster_name().c_str();
}

extern "C" void set_cluster_name(const char* cluster_name){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    flame_context->set_cluster_name(cluster_name);
    return ;
}

extern "C" const char* get_node_name(){
    flame::FlameContext* flame_context = flame::FlameContext::get_context(); 
    return flame_context->node_name().c_str();
}

extern "C" void set_node_name(const char* node_name){
    flame::FlameContext* flame_context = flame::FlameContext::get_context();
    flame_context->set_node_name(node_name);
    return ;
}

/* FlameStub_API*/
extern "C" void flame_stub_connect_mgr(const char* ip){
    FlameStub* flame_stub;
    std::string ip_s = ip;
    flame_stub = FlameStub::connect(ip_s);//manager地址

    if (flame_stub == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return ;
    }
}

extern "C" int flame_stub_open_volume(const char* volume_group, const char* volume, VolumeMeta_t* const volume_meta){
    FlameStub* flame_stub;
    std::string ip = "192.168.3.112:6677";
    flame_stub = FlameStub::connect(ip);//manager地址
    if (flame_stub == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    struct VolumeMeta meta = {0};
    Volume* res = new Volume(meta);
    flame_stub->vol_open("vg1", "vol1", &res);
    volume_meta->id = flame_stub->volume->get_meta().id;
    strcpy(volume_meta->name, flame_stub->volume->get_meta().name.c_str());
    strcpy(volume_meta->vg_name, flame_stub->volume->get_meta().vg_name.c_str());
    volume_meta->size = flame_stub->volume->get_meta().size;
    volume_meta->ctime = flame_stub->volume->get_meta().ctime;
    volume_meta->chk_sz = flame_stub->volume->get_meta().chk_sz;
    volume_meta->spolicy = flame_stub->volume->get_meta().spolicy;
    std::map<uint64_t, ChunkAddr>::iterator iter;
    std::map<uint64_t, ChunkAddr>* chunks = & (flame_stub->volume->get_meta().chunks_map);
    int chunks_num = chunks->size(), i;
    volume_meta->chunk_addr_pair = (ChunkAddrPair *)malloc(chunks_num * sizeof(ChunkAddrPair));
    for(i = 0, iter = chunks->begin(); iter != chunks->end(); ++iter, ++i){
        volume_meta->chunk_addr_pair[i].key = iter->first;
        volume_meta->chunk_addr_pair[i].chunkaddr = { iter->second.chunk_id, iter->second.ip, iter->second.port};
        std::string ip_s = ip32_to_string(iter->second.ip);
        flame_stub->cmd_client_stub->set_session(ip_s, iter->second.port);
    }
    return 0;
}

/* Buffer_API*/
extern "C" int get_buffer_addr(BufferInfo_t* const buffer_info, void** buf){
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
    Buffer* buffer = allocator->allocate_ptr(1 << 22); //4MB
    buffer_info->addr = (uint64_t)buffer->addr();
    buffer_info->size = buffer->size();
    buffer_info->type = buffer->type();
    *buf = (void*)buffer;
    return 0;
}

/*IO_API */
typedef void (*Callback)(void* arg1, void* arg2);

extern "C" int flame_write(void* buffer, const uint64_t offset, const uint64_t len, Callback cb, void* cb_arg){
    Buffer *buf = (Buffer *)buffer;
    FlameStub* flame_stub;
    std::string ip = "192.168.3.112:6677";
    flame_stub = FlameStub::connect(ip);//manager地址
    if (flame_stub == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    //TODO  需要合适得加入回调
    flame_stub->write(*buf, offset, len, nullptr, nullptr);
    return 0;
}

extern "C" int flame_read(void* buffer, const uint64_t offset, const uint64_t len, Callback cb, void* cb_arg){
    Buffer *buf = (Buffer *)buffer;
    FlameStub* flame_stub;
    std::string ip = "192.168.3.112:6677";
    flame_stub = FlameStub::connect(ip);//manager地址
    if (flame_stub == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    //TODO  需要合适得加入回调
    flame_stub->read(*buf, offset, len, nullptr, nullptr);
    return 0;
}

/*Other_API */
extern "C" uint64_t get_volume_id(struct VolumeMeta* volume_meta)
{
    FILE* pFile = fopen("123.txt", "a");
    write_log(pFile, "id = %u, success = %u", volume_meta->id, RC_SUCCESS);
    return volume_meta->id;
}

} //namespace