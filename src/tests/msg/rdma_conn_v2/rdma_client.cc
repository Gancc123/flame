/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-10-25 19:03:52
 */
#include "common/context.h"
#include "msg/msg_core.h"
#include "util/clog.h"

#include "msger.h"
#include "util/spdk_common.h"

#include <unistd.h>
#include <cstdio>

using FlameContext = flame::FlameContext;
using namespace flame::msg;

void send_first_incre_msg(MsgContext *mct, Msger *msger){
    NodeAddr *addr = new NodeAddr(mct);
    addr->ip_from_string("127.0.0.1");
    addr->set_port(7778);
    msger_id_t msger_id = msger_id_from_msg_node_addr(addr);
    auto session = mct->manager->get_session(msger_id);
    NodeAddr *rdma_addr = new NodeAddr(mct);
    rdma_addr->set_ttype(NODE_ADDR_TTYPE_RDMA);
    rdma_addr->ip_from_string("127.0.0.1");
    rdma_addr->set_port(7778);
    session->set_listen_addr(addr);
    session->set_listen_addr(rdma_addr, msg_ttype_t::RDMA);
    auto conn = session->get_conn(msg_ttype_t::RDMA);
    auto rdma_conn = RdmaStack::rdma_conn_cast(conn);

    auto req = msger->get_req_pool().alloc_req();
    assert(req);
    req->data->count = 0;
    rdma_conn->post_send(req);

    rdma_addr->put();
    addr->put();
}

#define CFG_PATH "flame_client.cfg"

static void msg_clear_done_cb(void *arg1, void *arg2){
    //free RdmaBuffers before RdmaStack detroyed.
    Msger *msger = (Msger *)arg1;
    msger->get_req_pool().purge(-1);
}

static void rdma_client_start(void *arg1, void *arg2){
    auto fct = FlameContext::get_context();
    if(!fct->init_config(CFG_PATH)){
        clog("init config failed.");
        return ;
    }
    if(!fct->init_log("", "TRACE", "client")){
        clog("init log failed.");
        return ;
    }

    auto mct = new MsgContext(fct);

    ML(mct, info, "init complete.");
    ML(mct, info, "load cfg: " CFG_PATH);

    if(mct->load_config()){
        assert(false);
    }

    auto msger = new Msger(mct, false);

    mct->clear_done_cb = msg_clear_done_cb;
    mct->clear_done_arg1 = msger;
    mct->config->set_msg_worker_type("SPDK");

    ML(mct, info, "before msg module init");
    mct->init(msger);
    ML(mct, info, "after msg module init");

    ML(mct, info, "msger_id {:x} {:x} ", mct->config->msger_id.ip,
                                         mct->config->msger_id.port);

    send_first_incre_msg(mct, msger);

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
    opts.rpc_addr = "/var/tmp/spdk_client.sock";
    int rc = 0;
    rc = spdk_app_start(&opts, rdma_client_start, nullptr, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS.\n");
    }
    spdk_app_fini();

    return 0;
}
