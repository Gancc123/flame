/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-07-15 09:13:38
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-19 14:52:22
 */


#ifndef LIBFLAME_API_H
#define LIBFLAME_API_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#if __GNUC__ >= 4
    #define FLAME_API __attribute__((visibility ("default")))
#else
    #define FLAME_API
#endif


/* FlameContext_API*/
typedef void flame_context_t;

FLAME_API bool get_flame_context(flame_context_t* context);

FLAME_API bool flame_context_init_cfg(const char* cfg_path);

FLAME_API bool flame_context_init_log(const char* dir, const char* level, const char* prefix);

FLAME_API const char* get_cluster_name();

FLAME_API void set_cluster_name(const char* cluster_name);

FLAME_API const char* get_node_name();

FLAME_API void set_node_name(const char* node_name);

/* FlameHandlers_API*/
typedef struct{
    uint64_t    chunk_id;
    uint32_t    ip;
    uint32_t    port;
}ChunkAddr_t;

typedef struct{
    uint64_t key;
    ChunkAddr_t chunkaddr;
}ChunkAddrPair;

typedef struct{
    uint64_t    id;
    char name[50];
    char vg_name[50];
    uint64_t    size;
    uint64_t    ctime; // create time
    bool        prealloc;
    uint64_t    chk_sz;
    uint32_t    spolicy;
    ChunkAddrPair* chunk_addr_pair;//保存chunk_index到ChunkAddr的映射，其中chunk_index从0开始
}VolumeMeta_t;

FLAME_API void flame_handlers_connect_mgr(const char* ip);

FLAME_API int flame_handlers_open_volume(const char* volume_group, const char* volume);

FLAME_API int flame_handlers_set_session(const char* ip, const int port);

/* Buffer_API*/
typedef struct{
    uint64_t addr;
    uint64_t size;
    int type;
}BufferInfo_t;

FLAME_API int allocate_buffer(BufferInfo_t* const buffer_info, void** buf);

/*IO_API */
typedef void (*libflame_callback)(void* arg, int status);

FLAME_API int flame_write(const char* volume_group, const char* volume, void* buffer, const uint64_t offset, const uint64_t len, libflame_callback cb, void* cb_arg);

FLAME_API int flame_read(const char* volume_group, const char* volume, void* buffer, const uint64_t offset, const uint64_t len, libflame_callback cb, void* cb_arg);

#ifdef __cplusplus
}
#endif

#endif // LIBFLAME_API_H