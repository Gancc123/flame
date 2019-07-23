#include "include/libflame.h"
#include "util/spdk_common.h"


using namespace flame;
using namespace flame::memory;
using namespace flame::memory::ib;
using namespace flame::msg;
using FlameContext = flame::FlameContext;

#define CFG_PATH "flame_client.cfg"


void cb_func(void* arg){
    char* mm = (char*)arg;
    for(int i = 0; i < 8192 * 2; i++){
        if(mm[i] == 0) std::cout << " ";
        else std::cout << mm[i];
    }
    std::cout << "111" << std::endl;
    return ;
}

void cb_func2(void* arg){
    std::cout << "222" << std::endl;
    return ;
}

static void test_gateway(void *arg1, void *arg2){
    FlameContext *flame_context = FlameContext::get_context();
    if(!flame_context->init_config(CFG_PATH)){
        return ;
    }
    if(!flame_context->init_log("", "TRACE", "client")){
        return ;
    }
    std::cout << "load config completed.." << std::endl;

    std::shared_ptr<FlameStub> flame_stub;
    std::string ip = "192.168.3.112:6677";
    flame_stub.reset(FlameStub::connect(ip));//manager地址

    if (flame_stub.get() == nullptr) {
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return ;
    }
    struct VolumeMeta volume_meta = {0};
    Volume* res = new Volume(volume_meta);
    flame_stub->vol_open("vg1", "vol1", &res);
    std::cout << "vol_id(): " << flame_stub->volume->get_meta().id<< std::endl;
    std::cout << "vol_name(): " << flame_stub->volume->get_meta().name<< std::endl;
    std::cout << "vg_name(): " << flame_stub->volume->get_meta().vg_name<< std::endl;
    std::cout << "vol_size(): " << flame_stub->volume->get_meta().size<< std::endl;
    std::cout << "ctime(): " << flame_stub->volume->get_meta().ctime<< std::endl;
    std::cout << "chk_sz(): " << flame_stub->volume->get_meta().chk_sz<< std::endl;
    std::cout << "spolicy(): " << flame_stub->volume->get_meta().spolicy<< std::endl;

    std::map<uint64_t, ChunkAddr>::iterator iter;
    std::cout << "-------------------------" << std::endl;
    for(iter = flame_stub->volume->get_meta().chunks_map.begin(); iter != flame_stub->volume->get_meta().chunks_map.end(); ++iter){
        std::cout << "index   : " << iter->first << std::endl;
        std::cout << "chunk_id: " << iter->second.chunk_id << std::endl;
        std::cout << "ip      : " << iter->second.ip << std::endl;
        std::cout << "port    : " << iter->second.port << std::endl;
        std::cout << "--------------------------" << std::endl;
    }
    flame_stub->cmd_client_stub->set_session("192.168.3.112", 9999);
    flame_stub->cmd_client_stub->set_session("192.168.3.112", 9996);
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
    Buffer buf_write = allocator->allocate(1 << 22); //4MB
    Buffer buf_read  = allocator->allocate(1 << 22); //4MB
    uint64_t GigaByte = 1 << 30;
    char *m = (char *)buf_write.addr();
    for(int i = 0; i < 8192 * 2; i++)
        *(m + i) = 'a' + i % 26;
    flame_stub->write(buf_write, GigaByte - 8192, 8192 * 2, cb_func2, nullptr);
    getchar();
    flame_stub->read(buf_read, GigaByte - 8192, 8192 * 2, cb_func, buf_read.addr());
    getchar();
    spdk_app_stop(0);
}

int main(int argc, char *argv[])
{
    int rc = 0;
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts);
    opts.name = "gateway_test";
    opts.reactor_mask = "0xf0";
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