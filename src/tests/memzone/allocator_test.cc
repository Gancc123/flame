#include "common/context.h"
#include "common/log.h"
#include "util/spdk_common.h"
#include "allocator_test.h"
#include "memzone/rdma_mz.h"

using namespace flame;
using namespace flame::memory;
using namespace flame::memory::ib;
using namespace flame::msg;

using FlameContext = flame::FlameContext;

#define KB 1024L
#define MB (1024L * (KB))
#define GB (1024L * (MB))
#define CFG_PATH "flame_mgr.cfg"

struct test_context {
    FlameContext *fct;
    MsgContext *mct;
    RdmaMsger *rdma_msger;
};

static void test_start(void *arg1, void *arg2) {
    struct test_context *tctx = (struct test_context*)arg1;
    std::cout << "test start..." << std::endl;

    FlameContext *fct = tctx->fct;
    MsgContext *mct = tctx->mct;
    
    mct->init(tctx->rdma_msger);

    std::cout << "mct init success..." << std::endl;

    MemoryConfig *mem_cfg = MemoryConfig::load_config(fct);
    assert(mem_cfg);

    std::cout << "load config completed.." << std::endl;

    flame::msg::ib::ProtectionDomain *pd = flame::msg::Stack::get_rdma_stack()->get_manager()->get_ib().get_pd();

    //初始化一个RDMA env然后将RDMA的pd作为参数传递给RdmaAllocator。
    BufferAllocator *allocator = new RdmaAllocator(fct, pd, mem_cfg);

    std::cout << "alloctor init completed.." << std::endl;

    Buffer rdma_buf = allocator->allocate(4 * MB);
    BufferPtr *rdma_buf_ptr = rdma_buf.get();

    if(rdma_buf_ptr == nullptr) {
        fct->log()->lerror("allocator_test", "allocator alloc memory failed.");
        rdma_buf.clear();
        delete allocator;
        spdk_app_stop(-1);
        return;
    } else {
        fct->log()->linfo("allocator_test", "allocator alloc memory success.");
        
        void *buffer = rdma_buf_ptr->addr();
        size_t size = rdma_buf_ptr->size();

        std::cout << "size = " << size << std::endl;
    }

    rdma_buf.clear();
    delete allocator;
    spdk_app_stop(0);
}

int main(int argc, char *argv[])
{

    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(std::string(CFG_PATH))) {
        clog("init config failed.");
        return -1;
    }

    if(!fct->init_log("", "TRACE", "allocator_test")){
        clog("init log failed.");
        return -1;
    }

    auto mct = new MsgContext(fct);
    
    auto rdma_msger = new RdmaMsger(mct);

    mct->load_config();
    int r = mct->config->set_msg_worker_type("SPDK");
    assert(!r);

    int rc = 0;
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts);
    opts.name = "allocator_test";
    opts.reactor_mask = "0x01";


    struct test_context *t_ctx = new test_context();
    t_ctx->fct = fct;
    t_ctx->mct = mct;
    t_ctx->rdma_msger = rdma_msger;
    rc = spdk_app_start(&opts, test_start, t_ctx, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS!\n");
    }

    spdk_app_fini();

    if(rdma_msger)
        delete rdma_msger;

    return rc;
}