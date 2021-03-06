/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-05 11:13:01
 */
#include "libflame_client.h"

#include "proto/libflame.grpc.pb.h"
#include "proto/libflame.pb.h"
#include "service/log_service.h"
#include "libflame/libchunk/libchunk.h"

#include <grpcpp/grpcpp.h>
#include <regex>
#include <string>
#include <iterator>

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

#if __GNUC__ >= 4
    #define FLAME_API __attribute__((visibility ("default")))
#else
    #define FLAME_API
#endif
namespace flame {

//FlameHandlers
FlameHandlers* FlameHandlers::g_flame_handlers = nullptr;
FlameHandlers* FlameHandlers::connect(const Config& cfg){
    if(g_flame_handlers != nullptr) return g_flame_handlers;
    /**将cfg.mgr_addr转换为uint32_t IP + uint32_t Port，gw_id用IP，admin_addr用(IP<<32 | Port)例如mgr_addr = "192.168.3.110:6666"**/
    uint32_t ip, port;
    std::regex pattern("([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})[:](\\d+)");
    std::smatch result;
    bool match = regex_search(cfg.mgr_addr, result, pattern);
    if(match)
    {
        for (int i = i; i < result.size(); i++)
        {
            if(i == 5){
                port = atoi(result[i].str().c_str());
                continue;
            }
            ip = ip << 8;
            ip = ip | atoi(result[i].str().c_str());
        }
    }
    FlameContext* flame_context = FlameContext::get_context();
    FlameHandlers* flame_handlers = new FlameHandlers(flame_context, ip, grpc::CreateChannel(
        cfg.mgr_addr, grpc::InsecureChannelCredentials())
    );
    g_flame_handlers = flame_handlers;
    return g_flame_handlers;
}

FlameHandlers* FlameHandlers::connect(std::string& path){
    if(g_flame_handlers != nullptr) return g_flame_handlers;
    /**将path转换为uint32_t IP + uint32_t Port，gw_id用IP，admin_addr用(IP<<32 | Port)例如mgr_addr = "192.168.3.110:6666"**/
    uint32_t ip, port;
    std::regex pattern("([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})[.]([0-9]{1,3})[:](\\d+)");
    std::smatch result;
    bool match = regex_search(path, result, pattern);
    if(match)
    {
        for (int i = i; i < result.size(); i++)
        {
            if(i == 5){
                port = atoi(result[i].str().c_str());
                continue;
            }
            ip = ip << 8;
            ip = ip | atoi(result[i].str().c_str());
        }
    }
    FlameContext* flame_context = FlameContext::get_context();
    g_flame_handlers = new FlameHandlers(flame_context, ip, grpc::CreateChannel(
        path, grpc::InsecureChannelCredentials())
    );
    return g_flame_handlers;
}

FlameHandlers* FlameHandlers::connect(){
    if(g_flame_handlers != nullptr) return g_flame_handlers;
    else return nullptr;
}

int FlameHandlers::disconnect(){
    CommonRequest req;
    req.set_gw_id(gw_id_);

    CommonReply reply;
    ClientContext ctx;
    Status stat = stub_->disconnect(&ctx, req, &reply);

    if (stat.ok()) {
        return reply.code();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}

// Cluster API
// return info of cluster by a argurment，现在暂时没管arg，默认返回所有集群信息
int FlameHandlers::cluster_info(const std::string& arg, flame::cluster_meta_t& res){
    CommonRequest req;
    req.set_gw_id(gw_id_);

    ClusterInfoReply reply;
    ClientContext ctx;
    Status stat = stub_->getClusterInfo(&ctx, req, &reply);

    if (stat.ok()) {
        res.name = reply.name();
        res.mgrs = reply.mgrs();
        res.csds = reply.csds();
        res.size = reply.size();
        res.alloced = reply.alloced();
        res.used = reply.used();
        return 0;
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}

// VolumeGroup API
// create an group.
int FlameHandlers::vg_create(const std::string& vg_name){
    VGCreateRequest req;
    req.set_vg_name(vg_name);

    CommonReply reply;
    ClientContext ctx;
    Status stat = stub_->createVolGroup(&ctx, req, &reply);

    if (stat.ok()) {
        return reply.code();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}
// list group. return an list of group name.
int FlameHandlers::vg_list(std::vector<volume_group_meta_t>& res){
    NoneParaRequest req;

    VGListReply reply;
    ClientContext ctx;
    Status stat = stub_->getVolGroupList(&ctx, req, &reply);

    if (stat.ok()) {
        for (uint64_t i = 0; i < reply.vg_list_size(); ++i) {
            volume_group_meta_t item;
            item.vg_id = reply.vg_list(i).vg_id();
            item.name = reply.vg_list(i).name();
            item.ctime = reply.vg_list(i).ctime();
            item.volumes = reply.vg_list(i).volumes();
            item.size = reply.vg_list(i).size();
            item.alloced = reply.vg_list(i).alloced();
            item.used = reply.vg_list(i).used();
            res.push_back(item);
        }
        return 0;
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}
// remove an group.
int FlameHandlers::vg_remove(const std::string& vg_name){
    VGRemoveRequest req;
    req.set_vg_name(vg_name);

    CommonReply reply;
    ClientContext ctx;
    Status stat = stub_->removeVolGroup(&ctx, req, &reply);

    if (stat.ok()) {
        return reply.code();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}
// // rename an group. not support now
// // int vg_rename(const std::string& src, const std::string& dst);

// Volume API
// create an volume.
int FlameHandlers::vol_create(const std::string& vg_name, const std::string& vol_name, const VolumeAttr& attr){
    VolCreateRequest req;
    req.set_vg_name(vg_name);
    req.set_vol_name(vol_name);
    req.set_chk_sz(attr.chk_sz);
    req.set_size(attr.size);
    req.set_flags(attr.flags);
    req.set_spolicy(attr.spolicy);

    CommonReply reply;
    ClientContext ctx;
    Status stat = stub_->createVolume(&ctx, req, &reply);

    if (stat.ok()) {
        return reply.code();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}
// list volumes. return an list of volume name.
int FlameHandlers::vol_list(const std::string& vg_name, std::vector<flame::volume_meta_t>& res){
    VolListRequest req;
    req.set_vg_name(vg_name);

    VolListReply reply;
    ClientContext ctx;
    Status stat = stub_->getVolumeList(&ctx, req, &reply);

    if (stat.ok()) {
        for (uint64_t i = 0; i < reply.vol_list_size(); ++i) {
            volume_meta_t item;
            item.vol_id = reply.vol_list(i).vol_id();
            item.vg_id = reply.vol_list(i).vg_id();
            item.name = reply.vol_list(i).name();
            item.ctime = reply.vol_list(i).ctime();
            item.chk_sz = reply.vol_list(i).chk_sz();
            item.size = reply.vol_list(i).size();
            item.alloced = reply.vol_list(i).alloced();
            item.used = reply.vol_list(i).used();
            item.flags = reply.vol_list(i).flags();
            item.spolicy = reply.vol_list(i).spolicy();
            item.chunks = reply.vol_list(i).chunks();
            res.push_back(item);
        }
        return 0;
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}
// remove an volume.
int FlameHandlers::vol_remove(const std::string& vg_name, const std::string& vol_name){
    VolRemoveRequest req;
    req.set_vg_name(vg_name);
    req.set_vol_name(vol_name);

    CommonReply reply;
    ClientContext ctx;
    Status stat = stub_->removeVolume(&ctx, req, &reply);

    if (stat.ok()) {
        return reply.code();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}

int FlameHandlers::vol_meta(const std::string& vg_name, const std::string& vol_name, VolumeMeta& info){
    VolInfoRequest req;
    req.set_vg_name(vg_name);
    req.set_vol_name(vol_name);

    VolInfoReply reply;
    ClientContext ctx;
    Status stat = stub_->getVolumeInfo(&ctx, req, &reply);

    if (stat.ok()) {
        uint32_t retcode = reply.retcode();
        if (retcode != 0)
            return retcode;
        Volume* volume;
        std::map<uint64_t, Volume *>::iterator iter=volumes.find(reply.vol().vol_id());
        if(iter != volumes.end()){
            volume = volumes[reply.vol().vol_id()];
        }
        else{
            volume = volume = new Volume();
            volumes[reply.vol().vol_id()] = volume;
        } 
        volume->set_id(reply.vol().vol_id());
        volume->set_name(reply.vol().name());
        // set_group(const std::string& vg_name);
        volume->set_size(reply.vol().size());
        volume->set_ctime(reply.vol().ctime());
        // set_prealloc(bool v); 
        return 0;
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
}

int FlameHandlers::vol_open(const std::string& vg_name, const std::string& vol_name, Volume** res){
    VolOpenRequest req;
    req.set_gw_id(gw_id_);
    req.set_vg_name(vg_name);
    req.set_vol_name(vol_name);

    VolOpenReply reply;
    ClientContext ctx;
    Status stat = stub_->openVolume(&ctx, req, &reply);

    ChunkAddr addr;
    if (stat.ok()) {
        Volume* volume;
        volume = new Volume();
        volume->volume_meta_.id =      reply.vol_id();
        volume->volume_meta_.name =    reply.vol_name();
        volume->volume_meta_.vg_name = reply.vg_name();
        volume->volume_meta_.size =    reply.vol_size();
        volume->volume_meta_.ctime =   reply.ctime();
        // volume->volume_meta_.prealloc = reply.prealloc();
        volume->volume_meta_.chk_sz =  reply.chk_sz();
        volume->volume_meta_.spolicy = reply.spolicy();

        for (uint64_t i = 0; i < reply.chunks_size(); ++i) {
            addr.chunk_id = reply.chunks(i).chunk_id();
            addr.ip = reply.chunks(i).ip();
            addr.port = reply.chunks(i).port();
            volume->volume_meta_.chunks_map[i] = addr; //如index = 0 => (chunk_id, ip, port)
        }
        volumes[volume->volume_meta_.id] = volume;
        return reply.retcode();
    } else {
        flame_context_->log()->lerror("RPC Failed(%d): %s", stat.error_code(), stat.error_message().c_str());
        return -stat.error_code();
    }
    return 0;
}
// close a volume
int FlameHandlers::vol_close(const std::string& vg_name, const std::string& vol_name){
    return 0;
}

} // namespace flame