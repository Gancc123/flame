/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-06 16:13:58
 */
#include "Infiniband.h"
#include "RdmaConnection.h"
#include "RdmaStack.h"
#include "msg/internal/byteorder.h"
#include "msg/internal/node_addr.h"
#include "msg/internal/errno.h"

#include <algorithm>
#include <iterator>

namespace flame{
namespace msg{

const uint32_t RDMA_RW_WORK_BUFS_LIMIT = 8;
const uint32_t RDMA_BATCH_SEND_WR_MAX = 32;
const uint8_t RDMA_QP_MAX_RD_ATOMIC = 4;

class InternalMsgSendWr : public RdmaSendWr{
    ibv_sge sge;
    ibv_send_wr send_wr;
    Msg *msg;
public:
    InternalMsgSendWr(Msg *m) : msg(m) {
        msg->get();
        memset(&send_wr, 0, sizeof(send_wr));
        send_wr.wr_id = reinterpret_cast<uint64_t>(this);
        send_wr.opcode = IBV_WR_SEND;
        send_wr.send_flags |= IBV_SEND_SIGNALED;
        send_wr.send_flags |= IBV_SEND_INLINE;
        send_wr.num_sge = 1;
        auto data_len = msg->get_data_len();
        assert(data_len <= FLAME_MSG_CMD_RESERVED_LEN);
        msg_cmd_t *cmd = new msg_cmd_t;
        assert(cmd);
        cmd->mh.len = 1;
        cmd->mh.cls = FLAME_MSG_CMD_CLS_MSG;
        cmd->mh.opcode = FLAME_MSG_HDR_OPCODE_DECLARE_ID;
        auto data_it = msg->data_iter();
        data_it.copy(cmd->content, data_len);
        sge.addr = reinterpret_cast<uint64_t>(cmd);
        sge.length = sizeof(*cmd);
        sge.lkey = 0;
        send_wr.sg_list = &sge;

    }
    ~InternalMsgSendWr(){
        char *buf = reinterpret_cast<char *>(sge.addr);
        delete [] buf;
        msg->put();
    }
    virtual ibv_send_wr *get_ibv_send_wr() override{
        return &send_wr;
    }
    virtual void on_send_done(ibv_wc &cqe) override{
        delete this;
    }
    virtual void on_send_cancelled(bool err, int eno=0) override{
        delete this;
    }

};

class CloseMsgSendWr : public RdmaSendWr{
    ibv_send_wr send_wr;
    RdmaConnection *conn;
    bool do_close;
public:
    CloseMsgSendWr(RdmaConnection *c, bool d)
    : conn(c), do_close(d){
        memset(&send_wr, 0, sizeof(send_wr));
        send_wr.wr_id = reinterpret_cast<uint64_t>(this);
        send_wr.num_sge = 0;
        send_wr.opcode = IBV_WR_SEND;
        send_wr.send_flags |= IBV_SEND_SIGNALED;
    }
    ~CloseMsgSendWr() {}

    virtual ibv_send_wr* get_ibv_send_wr() override{
        return &send_wr;
    }
    virtual void on_send_done(ibv_wc &cqe) override{
        if(cqe.status == IBV_WC_SUCCESS){
           if(do_close){
                conn->do_close();
            }
        }
        delete this;
    }
    virtual void on_send_cancelled(bool err, int eno=0) override{
        delete this;
    }
    bool is_do_close() { return do_close; } 
};

RdmaConnection::RdmaConnection(MsgContext *mct)
:Connection(mct),
 status(RdmaStatus::INIT),
 is_dead_pending(false),
 send_mutex(MUTEX_TYPE_ADAPTIVE_NP),
 recv_cur_msg_header_buffer(sizeof(flame_msg_header_t)){

}

RdmaConnection *RdmaConnection::create(MsgContext *mct, RdmaWorker *w, 
                                                            uint8_t sl){
    ib::Infiniband &ib = w->get_manager()->get_ib();
    auto qp = ib.create_queue_pair(mct, w->get_tx_cq(), w->get_rx_cq(), 
                                                            w->get_srq(),
                                                            IBV_QPT_RC);
    if(!qp) return nullptr;

    // remote_addr will be updated later. 
    // just use host_addr as remote_addr temporarily.
    auto conn = new RdmaConnection(mct);

    conn_id_t conn_id;
    conn_id.type = msg_ttype_t::RDMA;
    conn_id.id = qp->get_local_qp_number(); 
    conn->set_id(conn_id);

    conn->qp = qp;
    conn->rdma_worker = w;
    ib::IBSYNMsg &my_msg = conn->get_my_msg();
    my_msg.qpn = qp->get_local_qp_number();
    my_msg.psn = qp->get_initial_psn();
    my_msg.lid = ib.get_lid();
    my_msg.peer_qpn = 0;
    my_msg.sl = sl;
    my_msg.gid = ib.get_gid();

    w->reg_rdma_conn(my_msg.qpn, conn);
     
    qp->user_ctx = conn;

    return conn;
}

RdmaConnection::~RdmaConnection(){
    MLI(mct, info, "status: {}", status_str(status));

     while(!pending_send_wrs.empty()){
         RdmaSendWr *wr = pending_send_wrs.front();
         wr->on_send_cancelled(false);
         pending_send_wrs.pop_front();
     }

    if(qp){
        delete qp;
        qp = nullptr;
    }

    std::list<Msg *> msgs;
    {
        MutexLocker l(send_mutex);
        msgs.swap(msg_list);
    }

    for(auto msg : msgs){
        msg->put();
    }
    
    if(recv_cur_msg){
        recv_cur_msg->put();
        recv_cur_msg = nullptr;
    }

}

ssize_t RdmaConnection::send_msg(Msg *msg, bool more){
    if(status != RdmaStatus::INIT
        && status != RdmaStatus::CAN_WRITE){
        ML(mct, warn, "Conn can't send msg. State: {}", status_str(status));
        return -1;
    }

    if(msg->get_data_len() > FLAME_MSG_CMD_RESERVED_LEN) return -1;

    auto send_wr = new InternalMsgSendWr(msg);
    assert(send_wr);
    post_send(send_wr, more);
    return 0;

}

int RdmaConnection::activate(){
    if(active){
        return 0;
    }
    
    ibv_qp_attr qpa;
    int r;
    ib::Infiniband &ib = rdma_worker->get_manager()->get_ib();

    // now connect up the qps and switch to RTR
    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTR;
    qpa.path_mtu = ib::Infiniband::ibv_mtu_enum(
                                            mct->config->rdma_path_mtu);
    qpa.dest_qp_num = peer_msg.qpn;
    qpa.rq_psn = peer_msg.psn;
    qpa.max_dest_rd_atomic = RDMA_QP_MAX_RD_ATOMIC;
    qpa.min_rnr_timer = 12;
    //qpa.ah_attr.is_global = 0;
    qpa.ah_attr.is_global = 1;
    qpa.ah_attr.grh.hop_limit = 6;
    qpa.ah_attr.grh.dgid = peer_msg.gid;

    qpa.ah_attr.grh.sgid_index = ib.get_device()->get_gid_idx();

    qpa.ah_attr.dlid = peer_msg.lid;
    qpa.ah_attr.sl = my_msg.sl;
    qpa.ah_attr.grh.traffic_class = mct->config->rdma_traffic_class;
    qpa.ah_attr.src_path_bits = 0;
    qpa.ah_attr.port_num = (uint8_t)(ib.get_ib_physical_port());

    ML(mct, info, "Choosing gid_index {}, sl {}", 
                                    (int)qpa.ah_attr.grh.sgid_index,
                                    (int)qpa.ah_attr.sl);
    
    r = ibv_modify_qp(qp->get_qp(), &qpa, IBV_QP_STATE |
                                            IBV_QP_AV |
                                            IBV_QP_PATH_MTU |
                                            IBV_QP_DEST_QPN |
                                            IBV_QP_RQ_PSN |
                                            IBV_QP_MIN_RNR_TIMER |
                                            IBV_QP_MAX_DEST_RD_ATOMIC);
    
    if(r){
        ML(mct, error, "failed to transition to RTR state: {}",
                                                    cpp_strerror(errno));
        return -1;
    }

    ML(mct, info, "transition to RTR state successfully.");

    // now move to RTS
    qpa.qp_state = IBV_QPS_RTS;

    // How long to wait before retrying if packet lost or server dead.
    // Supposedly the timeout is 4.096us*2^timeout.  However, the actual
    // timeout appears to be 4.096us*2^(timeout+1), so the setting
    // below creates a 135ms timeout.
    qpa.timeout = 14;

    // How many times to retry after timeouts before giving up.
    qpa.retry_cnt = 7;

    // How many times to retry after RNR (receiver not ready) condition
    // before giving up. Occurs when the remote side has not yet posted
    // a receive request.
    qpa.rnr_retry = 7; // 7 is infinite retry.
    qpa.sq_psn = my_msg.psn;
    qpa.max_rd_atomic = RDMA_QP_MAX_RD_ATOMIC;

    r = ibv_modify_qp(qp->get_qp(), &qpa, IBV_QP_STATE |
                                            IBV_QP_TIMEOUT |
                                            IBV_QP_RETRY_CNT |
                                            IBV_QP_RNR_RETRY |
                                            IBV_QP_SQ_PSN |
                                            IBV_QP_MAX_QP_RD_ATOMIC);
    if (r) {
        ML(mct, error, "failed to transition to RTS state: {}", 
                                                        cpp_strerror(errno));
        return -1;
    }

    // the queue pair should be ready to use once the client has finished
    // setting up their end.
    ML(mct, info, "transition to RTS state successfully.");
    ML(mct, info, "QueuePair:{:p} with qp: {:p}", (void *)qp, 
                                                        (void *)qp->get_qp());
    ML(mct, trace, "qpn:{} state:{}", my_msg.qpn, 
                                    ib.qp_state_string(qp->get_state()));
    active = true;
    status = RdmaStatus::CAN_WRITE;
    
    this->post_send(nullptr);

    return 0;
}

void RdmaConnection::do_close(){
    status = RdmaStatus::CLOSED;
    this->get_listener()->on_conn_error(this);
    if(is_dead_pending){
        return;
    }
    is_dead_pending = true;
    rdma_worker->make_conn_dead(this);
}

void RdmaConnection::close(){
    if(status == RdmaStatus::CLOSED
        && status == RdmaStatus::ERROR){
        return;
    }

    if(status == RdmaStatus::CAN_WRITE){
        fin_v2(false);
    }
    return;
}

void RdmaConnection::fault(){
    if(status == RdmaStatus::ERROR){
        return;
    }
    status = RdmaStatus::ERROR;
    this->get_listener()->on_conn_error(this);
    if(is_dead_pending){
        return;
    }
    is_dead_pending = true;
    rdma_worker->make_conn_dead(this);
    
}

static void event_fn_post_send(void *arg1, void *arg2){
    RdmaConnection *conn = (RdmaConnection *)arg1;
    RdmaSendWr *wr = (RdmaSendWr *)arg2;
    conn->post_send(wr);
}

void RdmaConnection::post_send(RdmaSendWr *wr, bool more){
    if(status != RdmaStatus::INIT
        && status != RdmaStatus::CAN_WRITE
        && wr){
        ML(mct, warn, "Conn can't post send wr. State: {}", status_str(status));
        ibv_send_wr *it = wr->get_ibv_send_wr();
        ibv_send_wr *next;
        while(it){
            next = it->next;
            auto send_wr = reinterpret_cast<RdmaSendWr *>(it->wr_id);
            send_wr->on_send_cancelled(false);
            it = next;
        }
        return;
    }

    if(!rdma_worker->get_owner()->am_self()){
        rdma_worker->get_owner()->post_work(event_fn_post_send, this, wr);
        return;
    }
    uint32_t tx_queue_len = rdma_worker->get_manager()->get_ib()
                                                            .get_tx_queue_len();
    //push wrs to pending_send_wrs.
    if(wr){
        ibv_send_wr *ibv_wr = wr->get_ibv_send_wr();
        ibv_send_wr *it = ibv_wr;
        while(it){
            auto send_wr = reinterpret_cast<RdmaSendWr *>(it->wr_id);
            pending_send_wrs.push_back(send_wr);
            it = it->next;
        }
    }
    ML(mct, debug, "pending_send_wrs: {}", pending_send_wrs.size());

    if(pending_send_wrs.size() == 0 || more){
        return;
    }

    if(status == RdmaStatus::INIT
        || status == RdmaStatus::CLOSED
        || status == RdmaStatus::ERROR){
        return;
    }

    //prepare wrs.
    uint32_t can_post_cnt = qp->add_tx_wr_with_limit(pending_send_wrs.size(),
                                                        tx_queue_len, true);
    uint32_t i;
    for(i = 0;i + 1 < can_post_cnt;++i){
        pending_send_wrs[i]->get_ibv_send_wr()->next =
                                        pending_send_wrs[i+1]->get_ibv_send_wr();
    }
    pending_send_wrs[i]->get_ibv_send_wr()->next = nullptr;
    

    //ibv_post_send()
    ibv_send_wr *tgt_wr = pending_send_wrs[0]->get_ibv_send_wr();
    ibv_send_wr *bad_tx_wr = nullptr;
    uint32_t success_cnt = can_post_cnt;
    int eno = 0;
    if (ibv_post_send(qp->get_qp(), tgt_wr, &bad_tx_wr)) {
        eno = errno;
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
            
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        //when failed.
        if(bad_tx_wr){
            for(i = 0;i < can_post_cnt;++i){
                if(pending_send_wrs[i]->get_ibv_send_wr() == bad_tx_wr){
                    break;
                }
            }
            success_cnt = i;

            //some wrs not posted.
            qp->dec_tx_wr(can_post_cnt - success_cnt);

            //try left wrs.
            rdma_worker->get_owner()->post_work(event_fn_post_send, 
                                                this, nullptr);
        }
    }

    pending_send_wrs.erase(pending_send_wrs.begin(), 
                                        pending_send_wrs.begin() + success_cnt);
    if(bad_tx_wr){
        pending_send_wrs.front()->on_send_cancelled(true, eno); //bad_wr_request
        pending_send_wrs.pop_front(); // remove this
    }
}

void RdmaConnection::post_recv(RdmaRecvWr *wr){
    ibv_recv_wr *bad_rx_wr = nullptr;
    bool err = false;
    int eno = 0;
    if(!wr) return;
    if(qp->has_srq()){
        ML(mct, error, "Conn with srq can't post recv to qp.");
        bad_rx_wr = wr->get_ibv_recv_wr();
        goto failed;
    }
    if(status == RdmaStatus::CLOSED || status == RdmaStatus::ERROR){
        ML(mct, warn, "Conn can't post recv wr. State: {}", status_str(status));
        bad_rx_wr = wr->get_ibv_recv_wr();
        goto failed;
    }
    if(ibv_post_recv(qp->get_qp(), wr->get_ibv_recv_wr(), &bad_rx_wr)){
        err = true;
        eno = errno;
        if(errno == ENOMEM){
            ML(mct, error, "failed to recv data. "
                        "(most probably recv queue is full): {}",
                        cpp_strerror(errno));
            
        }
        goto failed;
    }
    return;
failed:
    ibv_recv_wr *it = bad_rx_wr, *next = nullptr;
    while(it){
        next = it->next;
        RdmaRecvWr *recv_wr = reinterpret_cast<RdmaRecvWr *>(it->wr_id);
        recv_wr->on_recv_cancelled(err, eno);
        it = next;
    }
}

int RdmaConnection::post_recvs(std::vector<RdmaRecvWr *> &wrs){
    if(wrs.empty()) return 0;
    uint32_t i = 0;
    while(i + 1 < wrs.size()){
        wrs[i]->get_ibv_recv_wr()->next = wrs[i+1]->get_ibv_recv_wr();
        ++i;
    }
    wrs[i]->get_ibv_recv_wr()->next = nullptr;
    post_recv(wrs[0]);
    return wrs.size();
}

void RdmaConnection::close_msg_arrive(){
    ML(mct, debug, "{} got remote close msg...", to_string());
    if(status == RdmaStatus::CLOSING_POSITIVE){
        do_close();
    }else if(status == RdmaStatus::INIT || status == RdmaStatus::CAN_WRITE){
        fin_v2(true);
    }
}

void RdmaConnection::event_fn_send_fin_msg(void *arg1, void *arg2){
    auto conn = (RdmaConnection *)arg1;
    auto fin_wr = (CloseMsgSendWr *)arg2;
    conn->post_send(fin_wr);
    conn->status = fin_wr->is_do_close() ?
                 RdmaStatus::CLOSING_PASSIVE :
                 RdmaStatus::CLOSING_POSITIVE;
}

void RdmaConnection::fin_v2(bool do_close){
    auto wr = new CloseMsgSendWr(this, do_close);
    assert(wr);
    if(!rdma_worker->get_owner()->am_self()){
        rdma_worker->get_owner()->post_work(event_fn_send_fin_msg, this, wr);
    }else{
        post_send(wr);
        status = do_close ?
                 RdmaStatus::CLOSING_PASSIVE :
                 RdmaStatus::CLOSING_POSITIVE;
    }
}


} //namespace msg
} //namespace flame
