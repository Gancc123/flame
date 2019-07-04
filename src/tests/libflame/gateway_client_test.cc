#include <unistd.h>
#include <cstdio>

#include "libflame/libchunk/libchunk.h"
#include "include/csdc.h"
#include "libflame/libchunk/log_libchunk.h"

#include "common/context.h"
#include "common/log.h"
#include "util/spdk_common.h"
#include "include/libflame.h"

using namespace flame;
using namespace flame::memory;
using namespace flame::memory::ib;
using namespace flame::msg;
using FlameContext = flame::FlameContext;

#define CFG_PATH "flame_client.cfg"

static void test_gateway(void *arg1, void *arg2){
    std::shared_ptr<FlameStub> flame_stub;
    std::string ip = "192.168.3.112:6677";
    flame_stub.reset(FlameStub::connect(ip));

    if (flame_stub.get() == nullptr) {
        clog("create flame=>mgr stub faild");
        return ;
    }
    struct VolumeMeta volume_meta = {0};
    Volume* res = new Volume(volume_meta);
    flame_stub->vol_open("vg1", "vol1", &res);
    std::cout << "vol_id(): " << flame_stub->volume_->get_meta().id<< std::endl;
    std::cout << "vol_name(): " << flame_stub->volume_->get_meta().name<< std::endl;
    std::cout << "vg_name(): " << flame_stub->volume_->get_meta().vg_name<< std::endl;
    std::cout << "vol_size(): " << flame_stub->volume_->get_meta().size<< std::endl;
    std::cout << "ctime(): " << flame_stub->volume_->get_meta().ctime<< std::endl;
    std::cout << "chk_sz(): " << flame_stub->volume_->get_meta().chk_sz<< std::endl;
    std::cout << "spolicy(): " << flame_stub->volume_->get_meta().spolicy<< std::endl;

    std::map<uint64_t, ChunkAddr>::iterator iter;
    std::cout << "-------------------------" << std::endl;
    std::map<uint64_t, ChunkAddr> temp = flame_stub->volume_->get_meta().chunks_map;
    for(iter = flame_stub->volume_->get_meta().chunks_map.begin(); iter != flame_stub->volume_->get_meta().chunks_map.end(); ++iter){
    // for(iter = temp.begin(); iter != temp.end(); ++iter){
        std::cout << "index   : " << iter->first << std::endl;
        std::cout << "chunk_id: " << iter->second.chunk_id << std::endl;
        std::cout << "ip      : " << iter->second.ip << std::endl;
        std::cout << "port    : " << iter->second.port << std::endl;
        std::cout << "--------------------------" << std::endl;
    }
    spdk_app_stop(0);
}

int main(int argc, char *argv[])
{
    int rc = 0;
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts);
    opts.name = "gateway_test";
    opts.reactor_mask = "0xf00";
    opts.rpc_addr = "/var/tmp/spdk_gateway_c.sock";

    rc = spdk_app_start(&opts, test_gateway, nullptr, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS!\n");
    }

    spdk_app_fini();

    return 0;
}