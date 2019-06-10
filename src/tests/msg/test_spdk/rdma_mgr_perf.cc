#include "msg/msg_core.h"
#include "common/context.h"
#include "util/fmt.h"
#include "util/clog.h"

#include "rdma_msger.h"

#include <unistd.h>
#include <cstdio>
#include <string>

#include "spdk/event.h"

using FlameContext = flame::FlameContext;
using namespace flame::msg;

perf_config_t global_config;

#define CFG_PATH "flame_mgr.cfg"

static MsgContext *g_mct = nullptr;
static RdmaMsger *g_msger = nullptr;

static void msg_stop(void *arg1, void *arg2){
    MsgContext *mct = (MsgContext *)arg1;
    mct->finally_fin();
    spdk_app_stop(0);
}

static void shutdown_cb(void){
    auto mct = g_mct;
    auto rdma_msger = g_msger;

    rdma_msger->clear_rw_buffers();

    mct->clear_done_cb = msg_stop;
    mct->clear_done_arg1 = mct;

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");
}

static void msg_run(void *arg1, void *arg2){
    MsgContext *mct = (MsgContext *)arg1;
    RdmaMsger *rdma_msger = (RdmaMsger *)arg2;

    ML(mct, info, "before msg module init");
    mct->init(rdma_msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    clog("app init done.");
    clog("type CTRL+C to quit.");
}

int main(int argc, char *argv[]){
    auto parser = init_parser();
    optparse::Values options = parser.parse_args(argc, argv);
    auto config_path = options.get("config");

    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(
            std::string(config_path).empty()?CFG_PATH:config_path)){
        clog("init config failed.");
        return -1;
    }
    if(!fct->init_log("", str2upper(std::string(options.get("log_level"))), 
                    fmt::format("mgr{}",  std::string(options.get("index"))))){
         clog("init log failed.");
        return -1;
    }

    auto mct = new MsgContext(fct);
    g_mct = mct;

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    global_config.num = (uint32_t)options.get("num");
    global_config.depth = (uint32_t)options.get("depth");
    global_config.result_file = std::string(options.get("result_file"));
    global_config.perf_type = perf_type_from_str(
                                            std::string(options.get("type")));
    global_config.use_imm_resp = (bool)options.get("imm_resp");
    global_config.inline_size = std::string(options.get("inline"));
    global_config.size = size_str_to_uint64(std::string(options.get("size")));
    
    global_config.no_thr_optimize = (bool)options.get("no_thr_opt");

    init_resource(global_config);

    auto rdma_msger = new RdmaMsger(mct, &global_config);
    g_msger = rdma_msger;

    mct->load_config();
    mct->config->set_msg_log_level(std::string(options.get("log_level")));
    mct->config->set_rdma_max_inline_data(std::string(options.get("inline")));
    int r = mct->config->set_msg_worker_type("SPDK");
    assert(!r);

    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.name = "rdma_mgr_perf_spdk";
    // opts.config_file = "/etc/flame/nvme.conf";
    opts.rpc_addr = "/var/tmp/rdma_mgr_perf_spdk.sock";
    opts.reactor_mask = "0x1e";
    opts.shutdown_cb = shutdown_cb;
    int rc = 0;
    rc = spdk_app_start(&opts, msg_run, mct, rdma_msger);
    if(rc){
        clog("spdk app start: error!");
    }

    spdk_app_fini();

    delete rdma_msger;

    fin_resource(global_config);

    delete mct;

    return 0;
}