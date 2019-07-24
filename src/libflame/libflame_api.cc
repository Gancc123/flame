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

namespace flame{

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

/* FlameHandlers_API*/
extern "C" void flame_handlers_connect_mgr(const char* ip){
    FlameHandlers* flame_handlers;
    std::string ip_s = ip;
    flame_handlers = FlameHandlers::connect(ip_s);//manager地址

    if (flame_handlers == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return ;
    }
}

extern "C" int flame_handlers_open_volume(const char* volume_group_name, const char* volume_name){
    FlameHandlers* flame_handlers;
    if(FlameHandlers::g_flame_handlers == nullptr) return -1;
    else flame_handlers = FlameHandlers::g_flame_handlers;
    if (flame_handlers == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    struct VolumeMeta meta = {0};
    Volume* res = new Volume(meta);
    int rc = 0;
    rc = flame_handlers->vol_open(volume_group_name, volume_name, &res);
    uint64_t volume_id;
    std::string vg_name(volume_group_name), name(volume_name);
    rc = flame_handlers->vol_name_2_id(vg_name, name, volume_id);
    if(rc != 0) return -2;
    std::map<uint64_t, ChunkAddr>* chunks = & (flame_handlers->volumes[volume_id]->get_meta().chunks_map);
    int chunks_num = chunks->size(), i = 0;
    std::map<uint64_t, ChunkAddr>::iterator iter = chunks->begin();
    for(; iter != chunks->end(); ++iter, ++i){
        std::string ip_s = ip32_to_string(iter->second.ip);
        flame_handlers->cmd_client_stub->set_session(ip_s, iter->second.port);
    }
    return 0;
}

/* Buffer_API*/
extern "C" int allocate_buffer(BufferInfo_t* const buffer_info, void** buf){
    if(buffer_info->size <= 0) return -1;
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
    Buffer* buffer = allocator->allocate_ptr(buffer_info->size); //4MB
    buffer_info->addr = (uint64_t)buffer->addr();
    buffer_info->size = buffer->size();
    buffer_info->type = buffer->type();
    *buf = (void*)buffer;
    return 0;
}

/*IO_API */
extern "C" int flame_write(const char* volume_group, const char* volume, void* buffer, const uint64_t offset, const uint64_t len, libflame_callback cb, void* cb_arg){
    Buffer *buf = (Buffer *)buffer;
    FlameHandlers* flame_handlers;
    std::string ip = "192.168.3.112:6677";
    flame_handlers = FlameHandlers::connect(ip);//manager地址
    if (flame_handlers == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    //TODO  需要合适得加入回调
    flame_handlers->write(volume_group, volume, *buf, offset, len, cb, cb_arg);
    return 0;
}

extern "C" int flame_read(const char* volume_group, const char* volume, void* buffer, const uint64_t offset, const uint64_t len, libflame_callback cb, void* cb_arg){
    Buffer *buf = (Buffer *)buffer;
    FlameHandlers* flame_handlers;
    std::string ip = "192.168.3.112:6677";
    flame_handlers = FlameHandlers::connect(ip);//manager地址
    if (flame_handlers == nullptr) {
        flame::FlameContext* flame_context = flame::FlameContext::get_context();
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return -1;
    }
    //TODO  需要合适得加入回调
    flame_handlers->read(volume_group, volume, *buf, offset, len, cb, cb_arg);
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