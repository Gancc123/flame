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

    std::shared_ptr<FlameHandlers> flame_handlers;
    std::string ip = "192.168.3.112:6677";
    flame_handlers.reset(FlameHandlers::connect(ip));//manager地址

    if (flame_handlers.get() == nullptr) {
        flame_context->log()->lerror("create flame=>mgr stub faild");
        return ;
    }

    Volume* res = new Volume();
    const std::string  vg_name = "vg1";
    const std::string  vol_name = "vol1";
    flame_handlers->vol_open(vg_name, vol_name, &res);
    int rc = 0;
    uint64_t volume_id;
    rc = flame_handlers->vol_name_2_id(vg_name, vol_name, volume_id); 
    Volume* volume = flame_handlers->volumes[volume_id];
    std::cout << "vol_id(): " << volume->get_meta().id<< std::endl;
    std::cout << "vol_name(): " << volume->get_meta().name<< std::endl;
    std::cout << "vg_name(): " << volume->get_meta().vg_name<< std::endl;
    std::cout << "vol_size(): " << volume->get_meta().size<< std::endl;
    std::cout << "ctime(): " << volume->get_meta().ctime<< std::endl;
    std::cout << "chk_sz(): " << volume->get_meta().chk_sz<< std::endl;
    std::cout << "spolicy(): " << volume->get_meta().spolicy<< std::endl;

    std::map<uint64_t, ChunkAddr>::iterator iter;
    std::cout << "-------------------------" << std::endl;
    for(iter = volume->get_meta().chunks_map.begin(); iter != volume->get_meta().chunks_map.end(); ++iter){
        std::cout << "index   : " << iter->first << std::endl;
        std::cout << "chunk_id: " << iter->second.chunk_id << std::endl;
        std::cout << "ip      : " << iter->second.ip << std::endl;
        std::cout << "port    : " << iter->second.port << std::endl;
        std::cout << "--------------------------" << std::endl;
    }
    flame_handlers->cmd_client_stub->set_session("192.168.3.112", 9999);
    flame_handlers->cmd_client_stub->set_session("192.168.3.112", 9996);
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
    Buffer buf_write = allocator->allocate(1 << 22); //4MB
    Buffer buf_read  = allocator->allocate(1 << 22); //4MB
    uint64_t GigaByte = 1 << 30;
    char *m = (char *)buf_write.addr();
    for(int i = 0; i < 8192 * 2; i++)
        *(m + i) = 'a' + i % 26;
    flame_handlers->write(vg_name, vol_name, buf_write, GigaByte - 8192, 8192 * 2, cb_func2, nullptr);
    getchar();
    flame_handlers->read(vg_name, vol_name, buf_read, GigaByte - 8192, 8192 * 2, cb_func, buf_read.addr());
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