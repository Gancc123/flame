#include "msg/msg_core.h"
#include "common/context.h"
#include "util/fmt.h"
#include "util/clog.h"

#include "mem_cpy.h"

#include <unistd.h>
#include <cstdio>
#include <string>

using FlameContext = flame::FlameContext;
using namespace flame;

msg::mem_cpy_config_t cfg;

#define CFG_PATH "flame_target.cfg"

int main(int argc, char *argv[]){
    auto parser = msg::init_parser();
    auto options = parser.parse_args(argc, argv);
    auto config_path = options.get("config");

    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(
            std::string(config_path).empty()?CFG_PATH:config_path)){
        clog("init config failed.");
        return -1;
    }
    if(!fct->init_log("", msg::str2upper(std::string(options.get("log_level"))), 
                fmt::format("target{}",  std::string(options.get("index"))))){
         clog("init log failed.");
        return -1;
    }

    cfg.is_tgt = true;
    cfg.is_optimized = (bool)options.get("optimize");
    cfg.num = (int)options.get("num");
    cfg.result_file = std::string(options.get("result_file"));
    cfg.perf_type = msg::perf_type_from_str(std::string(options.get("type")));
    cfg.size = msg::size_str_to_uint64(std::string(options.get("size")));

    auto mct = new msg::MsgContext(fct);
    ML(mct, info, "num:{} size:{}", cfg.num, cfg.size);

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    msg::init_resource(cfg, options);

    auto msger = new msg::MemCpyMsger(mct, &cfg);

    mct->load_config();
    mct->config->set_msg_log_level(std::string(options.get("log_level")));

    ML(mct, info, "before msg module init");
    mct->init(msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    msg::parse_target_ips(std::string(options.get("dst")), mct, cfg);

    msger->prep_rw_bufs();

    std::getchar();

    msger->free_rw_bufs();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete msger;

    msg::fin_resource(cfg);

    delete mct;

    return 0;
}