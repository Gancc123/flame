#include "msg/msg_core.h"
#include "common/context.h"
#include "util/clog.h"

#include "incre_msger.h"

#include <unistd.h>
#include <cstdio>

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

    ML(mct, info, "before msg module init");
    mct->init(incre_msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    std::getchar();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete incre_msger;

    delete mct;

    return 0;
}