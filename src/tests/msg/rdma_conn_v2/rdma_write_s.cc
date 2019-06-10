#include "msg/msg_core.h"
#include "common/context.h"
#include "util/clog.h"

#include "msger_write.h"

#include <unistd.h>
#include <cstdio>

using FlameContext = flame::FlameContext;
using namespace flame::msg;

#define CFG_PATH "flame_mgr.cfg"

static void msg_clear_done_cb(void *arg1, void *arg2){
    //free RdmaBuffers before RdmaStack detroyed.
    RwMsger *msger = (RwMsger *)arg1;
    msger->get_req_pool().purge(-1);
}

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

    assert(!mct->load_config());

    mct->config->set_rdma_conn_version("2");

    auto msger = new RwMsger(mct, true);

    mct->clear_done_cb = msg_clear_done_cb;
    mct->clear_done_arg1 = msger;

    ML(mct, info, "before msg module init");
    mct->init(msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    std::getchar();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete msger;

    delete mct;

    return 0;
}