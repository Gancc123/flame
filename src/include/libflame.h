/*
 * @Descripttion: 
 * @version: 
 * @Author: lwg
 * @Date: 2019-06-10 14:57:01
 * @LastEditors: lwg
 * @LastEditTime: 2019-07-23 17:09:08
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

typedef void (*libflame_callback)(void* arg1);

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
    friend class FlameStub;
    /*将对volume的逻辑访问位置转换成物理的每个chunk的访问地址*/
    int vol_to_chunks(uint64_t offset, uint64_t length, std::vector<ChunkOffLen>& chunk_positions);

    struct VolumeMeta volume_meta_;
    
}; // class Volume

class FlameStub {
public:
    static FlameStub* g_flame_stub;
    /**gRPC服务**/
    // Connect API
    static FlameStub* connect(const Config& cfg);
    static FlameStub* connect(std::string& path);
    // Disconnect API
    int disconnect();
    // Cluster API
    // return info of cluster by a argurment，现在暂时没管arg，默认返回所有集群信息
    int cluster_info(const std::string& arg, cluster_meta_t& res);
    // Group API
    // create an group.
    int vg_create(const std::string& vg_name);
    // list group. return an list of group name.
    int vg_list(std::vector<flame::volume_group_meta_t>& res);
    // remove an group.
    int vg_remove(const std::string& vg_name);
    // Volume API
    // create an volume.
    int vol_create(const std::string& vg_name, const std::string& vol_name, const VolumeAttr& attr);
    // list volumes. return an list of volume name.
    int vol_list(const std::string& vg_name, std::vector<flame::volume_meta_t>& res);
    // remove an volume.
    int vol_remove(const std::string& vg_name, const std::string& vol_name);
    // read info of volume.
    int vol_meta(const std::string& vg_name, const std::string& vol_name, VolumeMeta& info);
    // open a volume, and return the io context of volume.
    int vol_open(const std::string& vg_name, const std::string& vol_name, Volume** res);
    // close a volume, and return the io context of volume.
    int vol_close(const std::string& vg_name, const std::string& vol_name);
    
    /**一个FlameStub结构对应一个Volume，注意：下列函数均为获得了Volume句柄后进行的操作**/
    // Volume async io call
    int read(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int write(const Buffer& buff, uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int reset(uint64_t offset, uint64_t len, libflame_callback cb, void* arg);
    int flush(libflame_callback cb, void* arg);

    FlameStub(FlameContext* flame_context, uint64_t gw_id, std::shared_ptr<grpc::Channel> channel)
    : flame_context_(flame_context), gw_id_(gw_id), stub_(LibFlameService::NewStub(channel)) {
        volume.reset(new Volume());
        cmd_client_stub = CmdClientStubImpl::create_stub(nullptr);
    }

    ~FlameStub() {}

    std::unique_ptr<Volume> volume;
    std::shared_ptr<CmdClientStubImpl> cmd_client_stub;
private:
    FlameContext* flame_context_;
    uint64_t gw_id_;
    std::unique_ptr<LibFlameService::Stub> stub_; 

    FlameStub(const FlameStub& rhs) = delete;
    FlameStub& operator=(const FlameStub& rhs) = delete;

}; // class FlameStub


} // namespace flame

#endif // LIBFLAME_H