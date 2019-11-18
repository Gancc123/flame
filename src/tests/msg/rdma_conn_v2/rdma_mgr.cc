/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-10-25 19:04:01
 */
#include "msg/msg_core.h"
#include "common/context.h"
#include "util/clog.h"

#include "msger.h"
#include "util/spdk_common.h"

#include <unistd.h>
#include <cstdio>

using FlameContext = flame::FlameContext;
using namespace flame::msg;

#define CFG_PATH "flame_mgr.cfg"

static void msg_clear_done_cb(void *arg1, void *arg2){
    //free RdmaBuffers before RdmaStack detroyed.
    Msger *msger = (Msger *)arg1;
    msger->get_req_pool().purge(-1);
}

static void rdma_mgr_start(void *arg1, void *arg2){
    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_config(CFG_PATH)){
        clog("init config failed.");
        return ;
    }
    if(!fct->init_log("", "TRACE", "mgr")){
         clog("init log failed.");
        return ;
    }

    auto mct = new MsgContext(fct);

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    if(mct->load_config()){
        assert(false);
    }
    auto msger = new Msger(mct, true);

    mct->clear_done_cb = msg_clear_done_cb;
    mct->clear_done_arg1 = msger;
    mct->config->set_msg_worker_type("SPDK");

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
    
    spdk_app_stop(0);

    return ;
}

int main(int argc, char *argv[]) {
    // 初始化spdk应用程序启动的配置参数
    struct spdk_app_opts opts;
    spdk_app_opts_init(&opts);
    opts.mem_size = 2048;
    int rc = 0;
    rc = spdk_app_start(&opts, rdma_mgr_start, nullptr, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS.\n");
    }
    spdk_app_fini();

    return 0;
}