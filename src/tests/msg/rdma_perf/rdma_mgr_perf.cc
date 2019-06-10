#include "msg/msg_core.h"
#include "common/context.h"
#include "util/fmt.h"
#include "util/clog.h"

#include "rdma_msger.h"

#include <unistd.h>
#include <cstdio>
#include <string>

using FlameContext = flame::FlameContext;
using namespace flame::msg;

perf_config_t global_config;

#define CFG_PATH "flame_mgr.cfg"

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

    mct->load_config();
    mct->config->set_msg_log_level(std::string(options.get("log_level")));
    mct->config->set_rdma_max_inline_data(std::string(options.get("inline")));

    ML(mct, info, "before msg module init");
    mct->init(rdma_msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    std::getchar();

    rdma_msger->clear_rw_buffers();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete rdma_msger;

    fin_resource(global_config);

    delete mct;

    return 0;
}