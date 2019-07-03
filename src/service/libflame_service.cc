#include "libflame_service.h"
#include "include/retcode.h"
#include "include/libflame.h"
#include "util/utime.h"
#include "mgr/csdm/csd_mgmt.h"
#include "mgr/chkm/chk_mgmt.h"
#include "mgr/volm/vol_mgmt.h"

#include <string>
#include <list>
#include <map>

using grpc::ServerContext;
using grpc::Status;

using namespace std;

namespace flame {
namespace service {

/**
 * Interface for Service 
 */

//* Gateway Set
// GW注销：关闭一个GW连接
Status LibFlameServiceImpl::disconnect(ServerContext* context, 
const CommonRequest* request, CommonReply* response)
{
    // @@@ wait for gwm
    int gw_id = request->gw_id();
    int r = gw_ms->remove(gw_id);
    response->set_code(r);
    return Status::OK;
}
    
// 获取Flame集群信息
Status LibFlameServiceImpl::getClusterInfo(ServerContext* context, 
const CommonRequest* request, ClusterInfoReply* response)
{
    // @@@ wait for gwm
    cluster_meta_t cluster;
    cluster_ms->get(cluster);
    response->set_name(cluster.name);
    response->set_mgrs(cluster.mgrs);
    response->set_csds(cluster.csds);
    response->set_size(cluster.size);
    response->set_alloced(cluster.alloced);
    response->set_used(cluster.used);
    return Status::OK;
}

    
//* Group Set
// 获取所有VG信息，支持分页（需要提供<offset, limit>，以vg_name字典顺序排序）
Status LibFlameServiceImpl::getVolGroupList(ServerContext* context,
const NoneParaRequest* request, VGListReply* response)
{
    mct_->log()->ltrace("flame_service", "getVolGroupList");

    list<volume_group_meta_t> vgs;
    mct_->volm()->vg_list(vgs, 0, 0); //返回全部volume group

    for (auto it = vgs.begin(); it != vgs.end(); it++) {
        VGItem* item = response->add_vg_list();
        item->set_vg_id(it->vg_id);
        item->set_name(it->name);
        item->set_ctime(it->ctime);
        item->set_volumes(it->volumes);
        item->set_size(it->size);
        item->set_alloced(it->alloced);
        item->set_used(it->used);
    }
    return Status::OK;
}

// 创建VG
Status LibFlameServiceImpl::createVolGroup(ServerContext* context,
const VGCreateRequest* request, CommonReply* response)
{
    mct_->log()->ltrace("flame_service", "createVolGroup");

    int r = mct_->volm()->vg_create(request->vg_name());
    response->set_code(r);
    return Status::OK;
}

// 删除VG
Status LibFlameServiceImpl::removeVolGroup(ServerContext* context,
const VGRemoveRequest* request, CommonReply* response)
{
    mct_->log()->ltrace("flame_service", "removeVolGroup");

    int r = mct_->volm()->vg_remove(request->vg_name());
    response->set_code(r);
    return Status::OK;
}
    

//* Volume Set
// 获取指定VG内的所有Volume信息
Status LibFlameServiceImpl::getVolumeList(ServerContext* context,
const VolListRequest* request, VolListReply* response)
{
    mct_->log()->ltrace("flame_service", "getVolGroupList");

    list<volume_meta_t> vols;
    int r = mct_->volm()->vol_list(vols, request->vg_name(), 0, 0);
     
    for (auto it = vols.begin(); it != vols.end(); ++it) {
        VolumeItem* item = response->add_vol_list();
        item->set_vol_id(it->vol_id);
        item->set_vg_id(it->vg_id);
        item->set_name(it->name);
        item->set_ctime(it->ctime);
        item->set_chk_sz(it->chk_sz);
        item->set_size(it->size);
        item->set_alloced(it->alloced);
        item->set_used(it->used);
        item->set_flags(it->flags);
        item->set_spolicy(it->spolicy);
        item->set_chunks(it->chunks);
    }

    return Status::OK;
}
    
// 创建Volume
Status LibFlameServiceImpl::createVolume(ServerContext* context,
const VolCreateRequest* request, CommonReply* response)
{
    mct_->log()->ltrace("flame_service", "createVolume");

    vol_attr_t attr;
    attr.chk_sz = request->chk_sz();
    attr.size = request->size();
    attr.flags = request->flags();
    attr.spolicy = request->spolicy();

    int r = mct_->volm()->vol_create(request->vg_name(), request->vol_name(), attr);
    response->set_code(r);
    return Status::OK;
}

// 删除Volume
Status LibFlameServiceImpl::removeVolume(ServerContext* context,
const VolRemoveRequest* request, CommonReply* response)
{
    mct_->log()->ltrace("flame_service", "removeVolume");

    int r = mct_->volm()->vol_remove(request->vg_name(), request->vol_name());
    response->set_code(r);
    return Status::OK;
}
    
    
// 获取Volume信息
Status LibFlameServiceImpl::getVolumeInfo(ServerContext* context,
const VolInfoRequest* request, VolInfoReply* response)
{
    mct_->log()->ltrace("flame_service", "getVolumeInfo");

    volume_meta_t vol;
    int r = mct_->volm()->vol_info(vol, request->vg_name(), request->vol_name());
    response->set_retcode(r);

    if (r == RC_SUCCESS) {
        VolumeItem* item = response->mutable_vol();
        item->set_vol_id(vol.vol_id); 
        item->set_vg_id(vol.vg_id);  
        item->set_name(vol.name);   
        item->set_ctime(vol.ctime);  
        item->set_chk_sz(vol.chk_sz); 
        item->set_size(vol.size);   
        item->set_alloced(vol.alloced);
        item->set_used(vol.used);   
        item->set_flags(vol.flags);  
        item->set_spolicy(vol.spolicy);
        item->set_chunks(vol.chunks);
    }

    return Status::OK;
}

    
// 打开Volume：在MGR登记Volume访问信息（没有加载元数据信息）
Status LibFlameServiceImpl::openVolume(ServerContext* context,
const VolOpenRequest* request, VolOpenReply* response)
{
    mct_->log()->ltrace("flame_service", "openVolume");

    VolumeMeta volume_meta;
    int r = mct_->volm()->vol_open(request->gw_id(), request->vg_name(), request->vol_name(), volume_meta);

    // repeated Chunk chunks  = 10;  //index即第几块chunk
    response->set_retcode(r);
    response->set_vol_id(volume_meta.id);
    response->set_vol_name(volume_meta.name);
    response->set_vg_name(volume_meta.vg_name);
    response->set_vol_size(volume_meta.size);
    response->set_ctime(volume_meta.ctime);
    // response->set_prealloc(0);
    response->set_chk_sz(volume_meta.chk_sz);
    response->set_spolicy(volume_meta.spolicy);

    list<chunk_meta_t> chk_list;
    r = mct_->chkm()->info_vol(chk_list, volume_meta.id);
    //**这里的map一定要保证顺序，不能乱序了 **//
    map<uint64_t, uint64_t> chunk_csd_map;
    list<uint64_t> csd_ids;
    list<csd_addr_t> addrs;
    map<uint64_t, csd_addr_t> csd_addr_map;

    list<chunk_meta_t>::iterator elem;
    for (elem = chk_list.begin(); elem != chk_list.end(); ++elem) {
            chunk_csd_map[(*elem).chk_id] = (*elem).csd_id;
            if(find(csd_ids.begin(), csd_ids.end(), (*elem).csd_id) == csd_ids.end()){
                csd_ids.push_back((*elem).csd_id);
            }
    }
    csd_ids.sort(); // 默认升序排序？
    r = mct_->csdm()->csd_pull_addr(addrs, csd_ids);

    list<csd_addr_t>::iterator csd_elem;
    for (csd_elem = addrs.begin(); csd_elem != addrs.end(); ++csd_elem) {
            csd_addr_map[(*csd_elem).csd_id] = *csd_elem;
    }
    int i;
    for (i = 0, elem = chk_list.begin(); elem != chk_list.end(); ++i, ++elem) {
        Chunk* item = response->add_chunks();
        item->set_chunk_id((*elem).chk_id);
        uint64_t csd_id = chunk_csd_map[(*elem).chk_id];
        item->set_ip(csd_addr_map[csd_id].io_addr & 0xffffffff0000);//存储形式是32bit ip + 16bit port
        item->set_port(csd_addr_map[csd_id].io_addr & 0xffff);
    }
    return Status::OK;
}

// // 关闭Volume：在MGR消除Volume访问信息
// Status LibFlameServiceImpl::closeVolume(ServerContext* context,
// const VolCloseRequest* request, CommonReply* response)
// {
//     mct_->log()->ltrace("flame_service", "closeVolume");

//     int r = mct_->volm()->vol_close(request->gw_id(), request->vg_name(), request->vol_name());
//     response->set_code(r);
//     return Status::OK;
// }
    
    


} // namespace service
} // namespace flame