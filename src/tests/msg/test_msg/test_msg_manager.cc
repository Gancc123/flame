#include "msg/msg_core.h"
#include "common/context.h"
#include "util/clog.h"

#include "incre_msger.h"

using FlameContext = flame::FlameContext;
using namespace flame::msg;

#define CFG_PATH "flame_mgr.cfg"

int main(){
    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(CFG_PATH)){
        clog("init config failed.");
        return -1;
    }
    if(!fct->init_log("", "TRACE", "mgr")){
        clog("init log failed.");
        return -1;
    }

    auto mct = new MsgContext(fct);

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    auto incre_msger = new IncreMsger(mct);

    mct->init(incre_msger);

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    //test time work
    auto worker = mct->manager->get_worker(0);
    worker->post_time_work(100, [mct](){
        ML(mct, info, "test time work1: 100us");
    });

    worker->post_time_work(1000000, [mct](){
        ML(mct, info, "test time work2: 1000000us");
    });

    worker->post_time_work(5000000, [mct](){
        ML(mct, info, "test time work3: 5000000us");
    });

    std::getchar();

    mct->fin();

    delete incre_msger;

    delete mct;

    return 0;
}