/*
 * @Descripttion: 
 * @version: 
 * @Author: lwg
 * @Date: 2019-06-10 14:57:01
 * @LastEditors: lwg
 * @LastEditTime: 2019-07-29 14:52:07
 *
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef LIBFLAME_H
#define LIBFLAME_H

#include "include/retcode.h"
#include "include/buffer.h"
#include "memzone/rdma_mz.h"
#include "include/cmd.h"

#include "proto/libflame.grpc.pb.h"
#include "common/context.h"
#include "include/meta.h"
#include "service/libflame_service.h"
#include "include/cmd.h"
#include "libflame/libchunk/libchunk.h"

#include <grpcpp/grpcpp.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

namespace flame {

typedef void (*libflame_callback)(void* arg1, int status);

struct  Config {
    std::string mgr_addr;
};

//对单一chunk访问的结构
struct ChunkOffLen {
    uint64_t    chunk_id;
    uint64_t    offset;
    uint64_t    length;
};

//地址
struct ChunkAddr {
    uint64_t    chunk_id;
    uint32_t    ip;
    uint32_t    port;
};

struct VolumeAttr {
    uint64_t    size;
    bool        prealloc;
    uint64_t    chk_sz;
    uint32_t    flags  {0};
    uint32_t    spolicy;
};

struct VolumeMeta {
    uint64_t    id;
    std::string name;
    std::string vg_name;
    uint64_t    size;
    uint64_t    ctime; // create time
    bool        prealloc;
    uint64_t    chk_sz;
    uint32_t    spolicy;
    std::map<uint64_t, ChunkAddr> chunks_map;//保存chunk_index到ChunkAddr的映射，其中chunk_index从0开始
};

class Volume {
public:

    Volume(const VolumeMeta volume_meta)
        : volume_meta_(volume_meta){
    }
    
    Volume(){}

    ~Volume(){}

    uint64_t    get_id() const    { return volume_meta_.id; }
    std::string get_name() const  { return volume_meta_.name; }
    std::string get_vg_name() const { return volume_meta_.vg_name; }
    uint64_t    get_size() const  { return volume_meta_.size; }
    uint64_t    get_ctime() const  { return volume_meta_.ctime; }
    inline struct VolumeMeta& get_meta() {
        return volume_meta_;
    }
    void set_id(uint64_t id)     { volume_meta_.id = id; }
    void set_name(std::string name)   { volume_meta_.name = name; }
    void set_vg_name(std::string vg_name) { volume_meta_.vg_name = vg_name; }
    void set_size(uint64_t size)   { volume_meta_.size = size; }
    void set_ctime(uint64_t ctime)   { volume_meta_.ctime = ctime; }
    inline void set_meta(VolumeMeta volume_meta){
        volume_meta_ = volume_meta;        
    }

    // async io call
    int read(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int write(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int reset(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int flush(std::shared_ptr<CmdClientStubImpl> cmd_client_stub, libflame_callback cb, void* arg);

private:
    friend class FlameHandlers;
    int _vol_to_chunks(uint64_t offset, uint64_t length, std::vector<ChunkOffLen>& chunk_positions);/*将对volume的逻辑访问位置转换成物理的每个chunk的访问地址*/

    struct VolumeMeta volume_meta_;
    
}; // class Volume

class FlameHandlers {       //**FlameHandlers全局就一个g_flame_handlers，可以操纵多个volume **/
public:
    static FlameHandlers* g_flame_handlers;
    /**gRPC服务**/
    static FlameHandlers* connect(const Config& cfg);
    static FlameHandlers* connect(std::string& path);
    int disconnect();
    // TODO 现在暂时没管arg，默认返回所有集群信息
    int cluster_info(const std::string& arg, cluster_meta_t& res);
    // Group API
    int vg_create(const std::string& vg_name);
    int vg_list(std::vector<flame::volume_group_meta_t>& res);
    int vg_remove(const std::string& vg_name);
    // Volume API
    int vol_create(const std::string& vg_name, const std::string& vol_name, const VolumeAttr& attr);
    int vol_list(const std::string& vg_name, std::vector<flame::volume_meta_t>& res);
    int vol_remove(const std::string& vg_name, const std::string& vol_name);
    int vol_meta(const std::string& vg_name, const std::string& vol_name, VolumeMeta& info);
    int vol_open(const std::string& vg_name, const std::string& vol_name, Volume** res);
    int vol_close(const std::string& vg_name, const std::string& vol_name);

    int exist_volume(uint64_t volume_id);
    int id_2_vol_name(uint64_t volume_id, std::string& group_name, std::string& volume_name);
    int vol_name_2_id(const std::string& group_name, const std::string& volume_name, uint64_t& volume_id);
    /**注意：下列函数均为获得了Volume句柄后进行的操作**/
    // Volume async io call
    int read (const std::string& vg_name, const std::string& vol_name, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int write(const std::string& vg_name, const std::string& vol_name, const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int reset(const std::string& vg_name, const std::string& vol_name, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int flush(const std::string& vg_name, const std::string& vol_name, libflame_callback cb, void* arg);

    FlameHandlers(FlameContext* flame_context, uint64_t gw_id, std::shared_ptr<grpc::Channel> channel)
    : flame_context_(flame_context), gw_id_(gw_id), stub_(LibFlameService::NewStub(channel)) {
        cmd_client_stub = CmdClientStubImpl::create_stub(nullptr);
    }

    ~FlameHandlers() {}

    std::map <uint64_t, Volume *> volumes;
    std::shared_ptr<CmdClientStubImpl> cmd_client_stub;
private:
    FlameContext* flame_context_;
    uint64_t gw_id_;
    std::unique_ptr<LibFlameService::Stub> stub_; 

    FlameHandlers(const FlameHandlers& rhs) = delete;
    FlameHandlers& operator=(const FlameHandlers& rhs) = delete;

}; // class FlameHandlers
} // namespace flame

#endif // LIBFLAME_H