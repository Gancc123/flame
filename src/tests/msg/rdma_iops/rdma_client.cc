#include "msg/msg_core.h"
#include "common/context.h"
#include "util/fmt.h"
#include "util/clog.h"

#include "rdma_iops.h"

#include <unistd.h>
#include <cstdio>
#include <string>

using FlameContext = flame::FlameContext;
using namespace flame;

msg::rdma_iops_config_t cfg;

#define CFG_PATH "flame_client.cfg"

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
                fmt::format("client{}",  std::string(options.get("index"))))){
         clog("init log failed.");
        return -1;
    }

    cfg.is_mgr = false;
    cfg.use_imm_data = (bool)options.get("imm_data");
    cfg.num = msg::size_str_to_uint64(std::string(options.get("num")));
    cfg.depth = (unsigned long)options.get("depth");
    cfg.batch_size = (unsigned long)options.get("batch_size");

    auto mct = new msg::MsgContext(fct);

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    auto msger = new msg::IopsMsger(mct, &cfg);

    mct->load_config();
    mct->config->set_msg_log_level(std::string(options.get("log_level")));

    ML(mct, info, "before msg module init");
    mct->init(msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    msg::parse_target_ip(std::string(options.get("dst")), mct, cfg);

    msg::init_resource(mct, &cfg);

    mct->manager->get_worker(0)->post_time_work(1000, [msger, mct](){
        ML(mct, info, "start send req");
        cfg.cycles[0] = get_cycles();
        msger->send_req_batch();
    });

    std::getchar();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete msger;

    msg::fin_resource(&cfg);

    delete mct;

    return 0;
}