#include "common/context.h"
#include "msg/msg_core.h"
#include "util/clog.h"

#include "incre_msger.h"

#include <unistd.h>
#include <cstdio>

using FlameContext = flame::FlameContext;
using namespace flame::msg;

void send_first_incre_msg(MsgContext *mct){
    msg_incre_d data;
    data.num = 0;
    auto req_msg = Msg::alloc_msg(mct);
    req_msg->append_data(data);
    NodeAddr *addr = new NodeAddr(mct);
    addr->ip_from_string("127.0.0.1");
    addr->set_port(6666);
    msger_id_t msger_id = msger_id_from_msg_node_addr(addr);
    auto session = mct->manager->get_session(msger_id);
    NodeAddr *rdma_addr = new NodeAddr(mct);
    rdma_addr->set_ttype(NODE_ADDR_TTYPE_RDMA);
    rdma_addr->ip_from_string("127.0.0.1");
    rdma_addr->set_port(7777);
    session->set_listen_addr(addr);
    session->set_listen_addr(rdma_addr, msg_ttype_t::RDMA);
    auto conn = session->get_conn(msg_ttype_t::RDMA);
    conn->send_msg(req_msg);
    req_msg->put();
    rdma_addr->put();
    addr->put();
}

#define CFG_PATH "flame_clinet.cfg"

int main(){
    auto fct = FlameContext::get_context();
    if(!fct->init_config(CFG_PATH)){
        clog("init config failed.");
        return -1;
    }
    if(!fct->init_log("", "TRACE", "client")){
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

    send_first_incre_msg(mct);

    std::getchar();

    ML(mct, info, "before msg module fin");
    mct->fin();
    ML(mct, info, "after msg module fin");

    delete incre_msger;

    delete mct;

    return 0;
}