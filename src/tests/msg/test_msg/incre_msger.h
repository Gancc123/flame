#ifndef FLAME_TESTS_MSG_TEST_MSG_H
#define FLAME_TESTS_MSG_TEST_MSG_H

#include "msg/msg_core.h"
#define M_ENCODE(bl, data) (bl).append(&(data), sizeof(data))
#define M_DECODE(it, data) (it).copy(&(data), sizeof(data))

namespace flame {
namespace msg{

struct msg_incre_d : public MsgData{
    int num;
    virtual int encode(MsgBufferList &bl) override;
    virtual int decode(MsgBufferList::iterator &it) override;
};


class IncreMsger : public MsgerCallback{
    MsgContext *mct;
public:
    friend class IncreCb;
    explicit IncreMsger(MsgContext *c) : mct(c) {};
    virtual void on_conn_recv(Connection *conn, Msg *msg) override;
    virtual void on_rdma_env_ready() override;
};

int msg_incre_d::encode(MsgBufferList &bl){
    return M_ENCODE(bl, num);
}

int msg_incre_d::decode(MsgBufferList::iterator &it){
    return M_DECODE(it, num);
}

void IncreMsger::on_conn_recv(Connection *conn, Msg *msg){
    msg_incre_d data;
    auto it = msg->data_buffer_list().begin();
    data.decode(it);
    ++data.num;
    auto msger_id = conn->get_session()->peer_msger_id;
    ML(mct, info, "[{:x} {:x}] ->  incre num {} -> {}",
                            msger_id.ip, msger_id.port, data.num - 1, data.num);
    if(data.num >= 200){
        return;
    }

    auto req_msg = Msg::alloc_msg(mct);
    req_msg->append_data(data);
    conn->send_msg(req_msg);
    req_msg->put();
    return;
}

void IncreMsger::on_rdma_env_ready(){
    ML(mct, info, "RDMA Env ready!");
}

} //namespace msg
} //namespace flame

#undef M_ENCODE
#undef M_DECODE

#endif 