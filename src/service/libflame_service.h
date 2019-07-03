#ifndef FLAME_SERVICE_LIBFLAME_H
#define FLAME_SERVICE_LIBFLAME_H

#include "proto/libflame.pb.h"
#include "proto/libflame.grpc.pb.h"

#include "mgr/mgr_service.h"
#include "include/meta.h"
#include "metastore/metastore.h"

#include <grpcpp/grpcpp.h>

namespace flame {
namespace service {

class LibFlameServiceImpl final :  public LibFlameService::Service, public MgrService {
public:
    LibFlameServiceImpl(MgrContext* mct) : LibFlameService::Service(), MgrService(mct) {}

    /**
     * Interface for Service 
     */

    //* Gateway Set
    // GW注销：关闭一个GW连接
    virtual ::grpc::Status disconnect(::grpc::ServerContext* context, const ::CommonRequest* request, ::CommonReply* response);
    // 获取Flame集群信息
    virtual ::grpc::Status getClusterInfo(::grpc::ServerContext* context, const ::CommonRequest* request, ::ClusterInfoReply* response);
    //* Group Set
    // 获取所有VG信息，支持分页（需要提供<offset, limit>，以vg_name字典顺序排序）
    virtual ::grpc::Status getVolGroupList(::grpc::ServerContext* context, const ::NoneParaRequest* request, ::VGListReply* response);
    // 创建VG
    virtual ::grpc::Status createVolGroup(::grpc::ServerContext* context, const ::VGCreateRequest* request, ::CommonReply* response);
    // 删除VG
    virtual ::grpc::Status removeVolGroup(::grpc::ServerContext* context, const ::VGRemoveRequest* request, ::CommonReply* response);
    //* Volume Set
    // 获取指定VG内的所有Volume信息
    virtual ::grpc::Status getVolumeList(::grpc::ServerContext* context, const ::VolListRequest* request, ::VolListReply* response);
    // 创建Volume
    virtual ::grpc::Status createVolume(::grpc::ServerContext* context, const ::VolCreateRequest* request, ::CommonReply* response);
    // 删除Volume
    virtual ::grpc::Status removeVolume(::grpc::ServerContext* context, const ::VolRemoveRequest* request, ::CommonReply* response);
    // 重命名Volume
    virtual ::grpc::Status getVolumeInfo(::grpc::ServerContext* context, const ::VolInfoRequest* request, ::VolInfoReply* response);
    // 更改Volume大小
    virtual ::grpc::Status openVolume(::grpc::ServerContext* context, const ::VolOpenRequest* request, ::VolOpenReply* response);
protected:
    ClusterMS* cluster_ms {mct_->ms()->get_cluster_ms()};
    VolumeGroupMS* vg_ms {mct_->ms()->get_vg_ms()};
    VolumeMS* vol_ms {mct_->ms()->get_volume_ms()};
    ChunkMS* chk_ms {mct_->ms()->get_chunk_ms()};
    ChunkHealthMS* chk_hlt_ms {mct_->ms()->get_chunk_health_ms()};
    CsdMS* csd_ms {mct_->ms()->get_csd_ms()};
    CsdHealthMS* csd_hlt_ms {mct_->ms()->get_csd_health_ms()};
    GatewayMS* gw_ms {mct_->ms()->get_gw_ms()};

}; // class LibFlameServiceImpl

} // namespace service
} // namespace flame

#endif // FLAME_SERVICE_LIBFLAME_H
