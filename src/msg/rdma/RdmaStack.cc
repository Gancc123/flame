#include "RdmaStack.h"
#include "Infiniband.h"
#include "RdmaListenPort.h"
#include "RdmaConnection.h"
#include "RdmaPrepConn.h"
#include "msg/msg_def.h"
#include "msg/MsgManager.h"
#include "msg/internal/errno.h"

#include <cassert>

namespace flame{
namespace msg{

static const int RDMA_CQ_MAX_BATCH_COMPLETIONS = 16;

void RdmaTxCqNotifier::read_cb() {
    worker->process_cq_dry_run();
}

void RdmaRxCqNotifier::read_cb() {
    worker->process_cq_dry_run();
}

void RdmaAsyncEventHandler::read_cb() {
    manager->handle_async_event();
}

RdmaWorker::~RdmaWorker(){
    //ack events, otherwise the ibv_destroy_cq will failed.
    if(tx_cc){
        tx_cc->ack_events();
    }
    if(rx_cc){
        rx_cc->ack_events();
    }
    if(tx_cq){
        delete tx_cq;
        tx_cq = nullptr;
    }
    if(rx_cq){
        delete rx_cq;
        rx_cq = nullptr;
    }
    if(tx_cc){
        delete tx_cc;
        tx_cc = nullptr;
    }
    if(rx_cc){
        delete rx_cc;
        rx_cc = nullptr;
    }

    if(srq){
        int r = ibv_destroy_srq(srq);
        assert(r == 0);
        srq = nullptr;
    }

    while(!inflight_recv_wrs.empty()){
        RdmaRecvWr *wr = inflight_recv_wrs.front();
        wr->on_recv_cancelled(false);
        inflight_recv_wrs.pop_front();
    }
}

int RdmaWorker::init(){
    ib::Infiniband &ib = manager->get_ib();
    memory_manager = ib.get_memory_manager();

    if(mct->config->rdma_poll_event){
        tx_cc = ib.create_comp_channel(mct);
        assert(tx_cc);
        rx_cc = ib.create_comp_channel(mct);
        assert(rx_cc);
    }
    tx_cq = ib.create_comp_queue(mct, tx_cc);
    assert(tx_cq);
    rx_cq = ib.create_comp_queue(mct, rx_cc);
    assert(rx_cq);

    if(mct->config->rdma_enable_srq){
        srq = ib.create_shared_receive_queue(ib.get_rx_queue_len());
        if(!srq){
            return 1;
        }
        if(mct->config->rdma_conn_version == 1){
            ib.post_chunks_to_srq(ib.get_rx_queue_len(), srq);
        }else if(mct->config->rdma_conn_version == 2){
            post_rdma_recv_wr_to_srq(ib.get_rx_queue_len());
        }
    }

    return 0;
}

int RdmaWorker::clear_before_stop(){
    for(auto &it : qp_conns){
        it.second->close();
    }

    is_fin = true;

    if(qp_conns.empty()){
        get_manager()->worker_clear_done_notify();
    }
    return 0;
}

void RdmaWorker::clear_qp(uint32_t qpn){
    //remove conn from qp_conns map, and dec its refcount.
    int r = reg_rdma_conn(qpn, nullptr);
    //if no qp_conn and  is_fin, signal RdmaWorker.
    if(is_fin && qp_conns.empty() && r == -1){
        remove_notify();
        get_manager()->worker_clear_done_notify();
    }
}

int RdmaWorker::process_cq_dry_run(){
    static int MAX_COMPLETIONS = RDMA_CQ_MAX_BATCH_COMPLETIONS;
    ibv_wc wc[MAX_COMPLETIONS];
    int total_proc = 0;
    bool has_cq_event = false;
    int tx_ret = 0;
    int rx_ret = 0;

    has_cq_event = false;
    while(true){
        //drain channel fd
        if(!rx_cc->get_cq_event()){
            break;
        }
        has_cq_event = true;
    }
    if(has_cq_event){
        rx_cq->rearm_notify();
    }
    
    has_cq_event = false;
    while(true){
        //drain channel fd
        if(!tx_cc->get_cq_event()){
            break;
        }
        has_cq_event = true;
    }
    if(has_cq_event){
        tx_cq->rearm_notify();
    }

    do{
        rx_ret = process_rx_cq(wc, MAX_COMPLETIONS);
        tx_ret = process_tx_cq(wc, MAX_COMPLETIONS);
        total_proc += (rx_ret + tx_ret);
    }while(rx_ret + tx_ret > 0);

    reap_dead_conns();

    return total_proc;
}

void RdmaWorker::handle_tx_cqe(ibv_wc *cqe, int n){
    std::vector<Chunk*> tx_chunks;
    std::set<RdmaConnection *> to_wake_conns;
    auto tx_queue_len = manager->get_ib().get_tx_queue_len();

    if(mct->config->rdma_conn_version == 2){
        for(int i = 0;i < n; ++i){
            ibv_wc* response = &cqe[i];

            auto conn = get_rdma_conn(response->qp_num);
            assert(conn);
            ib::QueuePair *qp = conn->get_qp();
            if(qp){
                if(qp->get_tx_wr()){
                    //wakeup conn after dec_tx_wr;
                    to_wake_conns.insert(conn);
                }
                qp->dec_tx_wr(1);
            }

            ML(mct, debug, "QP: {}, wr_id: {:x}, imm_data:{} {} {}", 
                    response->qp_num, response->wr_id, 
                    (response->wc_flags & IBV_WC_WITH_IMM)?response->imm_data:0,
                    manager->get_ib().wc_opcode_string(response->opcode),
                    manager->get_ib().wc_status_to_string(response->status));

            RdmaSendWr *wr = reinterpret_cast<RdmaSendWr *>(response->wr_id);
            wr->get_ibv_send_wr()->next = nullptr;
            wr->on_send_done(cqe[i]);
            

            if (response->status != IBV_WC_SUCCESS) {
                if (response->status == IBV_WC_RETRY_EXC_ERR) {
                    ML(mct, error, "Connection between server and client not"
                                    " working. Disconnect this now");
                } else if (response->status == IBV_WC_WR_FLUSH_ERR) {
                    ML(mct, error, "Work Request Flushed Error: this connection's "
                        "qp={} should be down while this WR={:x} still in flight.",
                        response->qp_num, response->wr_id);
                } else {
                    ML(mct, error, "send work request returned error for "
                        "buffer({}) status({}): {}",
                        (void *)response->wr_id, response->status,
                        manager->get_ib().wc_status_to_string(response->status));
                }

                ML(mct, info, "qp state: {}", conn->get_qp_state());
                conn->fault();
            }
        }
        for(RdmaConnection *conn : to_wake_conns){
            if(conn->is_error() || conn->is_closed()) continue;
            conn->post_send(nullptr);
        }
        return;
    }

    for (int i = 0; i < n; ++i) {
        ibv_wc* response = &cqe[i];

        auto conn = get_rdma_conn(response->qp_num);
        assert(conn);
        ib::QueuePair *qp = conn->get_qp();
        if(qp){
            if(qp->get_tx_wr()){
                //wakeup conn after dec_tx_wr;
                to_wake_conns.insert(conn);
            }
            if(is_sel_sig_wrid(response->wr_id)){
                qp->dec_tx_wr(num_from_sel_sig_wrid(response->wr_id));
                ML(mct, info, "dec {}", num_from_sel_sig_wrid(response->wr_id));
            }else{
                qp->dec_tx_wr(1);
            }
        }

        //ignore rdma_write by post_imm_data().
        if(response->wr_id != 0 && !is_sel_sig_wrid(response->wr_id)
            && (response->opcode == IBV_WC_RDMA_READ
                || response->opcode == IBV_WC_RDMA_WRITE)){
            handle_rdma_rw_cqe(*response, conn);
            continue;
        }
        
        Chunk* chunk = nullptr;
        if(!is_sel_sig_wrid(response->wr_id)){
            chunk = reinterpret_cast<Chunk *>(response->wr_id);
        }
        ML(mct, info, "QP: {}, addr: {:p}, imm_data:{} {} {}", response->qp_num, 
                    (void *)chunk, 
                    (response->wc_flags & IBV_WC_WITH_IMM)?response->imm_data:0,
                    manager->get_ib().wc_opcode_string(response->opcode),
                    manager->get_ib().wc_status_to_string(response->status));

        if (response->status != IBV_WC_SUCCESS) {
            if (response->status == IBV_WC_RETRY_EXC_ERR) {
                ML(mct, error, "Connection between server and client not"
                                " working. Disconnect this now");
            } else if (response->status == IBV_WC_WR_FLUSH_ERR) {
                ML(mct, error, "Work Request Flushed Error: this connection's "
                    "qp={} should be down while this WR={:x} still in flight.",
                    response->qp_num, response->wr_id);
            } else {
                ML(mct, error, "send work request returned error for "
                    "buffer({}) status({}): {}",
                    (void *)response->wr_id, response->status,
                    manager->get_ib().wc_status_to_string(response->status));
            }

            ML(mct, info, "qp state: {}", conn->get_qp_state());
            conn->fault();
        }

        //TX completion may come either from regular send message or from 'fin'
        // message or from imm_data send. 
        //In the case of 'fin' wr_id points to the QueuePair.
        //In the case of imm_data send, wr_id is 0 or it has a magic prefix.
        //In the case of regular send message, wr_id is the tx_chunks pointer.
        if(response->wr_id == 0 || is_sel_sig_wrid(response->wr_id)){
            //ignore.
        }else if(reinterpret_cast<ib::QueuePair*>(response->wr_id) == qp){
            ML(mct, debug, "sending of the disconnect msg completed");
        }else {
            tx_chunks.push_back(chunk);
        }
    }

    for(RdmaConnection *conn : to_wake_conns){
        if(conn->is_error() || conn->is_closed()) continue;
        conn->send_msg(nullptr);
    }

    // owner->post_work([to_wake_conns, this](){
    //     ML(this->mct, trace, "in handle_tx_cqe()");
    //     for(RdmaConnection *conn : to_wake_conns){
    //         conn->send_msg(nullptr);
    //     }
    // });

    memory_manager->release_buffers(tx_chunks);
}

void RdmaWorker::handle_rdma_rw_cqe(ibv_wc &wc, RdmaConnection *conn){

    ML(mct, info, "QP: {} len: {}, wr_id: {:x} {} {}", wc.qp_num,
                    wc.byte_len, wc.wr_id,
                    ib::Infiniband::wc_opcode_string(wc.opcode), 
                    ib::Infiniband::wc_status_to_string(wc.status));
    
    RdmaRwWork *work = reinterpret_cast<RdmaRwWork *>(wc.wr_id);

    if (wc.status != IBV_WC_SUCCESS) {
        if (wc.status == IBV_WC_RETRY_EXC_ERR) {
            ML(mct, error, "Connection between server and client not"
                            " working. Disconnect this now");
        } else if (wc.status == IBV_WC_WR_FLUSH_ERR) {
            ML(mct, error, "Work Request Flushed Error: this connection's "
                "qp={} should be down while this WR={:x} still in flight.",
                wc.qp_num, wc.wr_id);
        } else if (wc.status == IBV_WC_REM_ACCESS_ERR) {
            ML(mct, error, "Remote Access Error: qp: {}, wr_id: {:x}", 
                wc.qp_num, wc.wr_id);
        } else {
            ML(mct, error, "send work request returned error for "
                "buffer({}) status({}): {}",
                wc.wr_id, wc.status,
                ib::Infiniband::wc_status_to_string(wc.status));
        }

        ML(mct, info, "qp state: {}", conn->get_qp_state());
        conn->fault();
        work->failed_indexes.push_back(work->lbufs.size() - work->cnt);
    }

    --work->cnt;

    if(work->cnt == 0){
        work->callback(conn);
    }

}

int RdmaWorker::process_tx_cq_dry_run(){
    static int MAX_COMPLETIONS = RDMA_CQ_MAX_BATCH_COMPLETIONS;
    ibv_wc wc[MAX_COMPLETIONS];
    int total_proc = 0;
    int tx_ret = 0;

    while(true){
        //drain channel fd
        if(!tx_cc->get_cq_event()){
            break;
        }
    }
    tx_cq->rearm_notify();

    do{
        tx_ret = process_tx_cq(wc, MAX_COMPLETIONS);
        total_proc += tx_ret;
    }while(tx_ret > 0);

    reap_dead_conns();

    return total_proc;
}

int RdmaWorker::process_tx_cq(ibv_wc *wc, int max_cqes){
    int tx_ret = tx_cq->poll_cq(max_cqes, wc);
    if(tx_ret > 0){
        ML(mct, info, "tx completion queue got {} responses.", tx_ret);
        handle_tx_cqe(wc, tx_ret);
        return tx_ret;
    }
    //here may be error or poll no cqe.
    return 0;
}

void RdmaWorker::reap_dead_conns(){
    auto dead_it = dead_conns.begin();
    while(dead_it != dead_conns.end()){
        auto dead_conn = *dead_it;
        if(dead_conn->get_tx_wr() && !dead_conn->is_error()){
            ML(mct, debug, "bypass conn={:p} tx_wr={}", (void *)dead_conn,
                                                        dead_conn->get_tx_wr());
            ++dead_it;
        }else{
            ML(mct, debug, "finally release conn={:p}", (void *)dead_conn);
            dead_conn->get_qp()->to_dead();
            dead_it = dead_conns.erase(dead_it);
        }
    }
}

inline int RdmaWorker::handle_rx_msg(ibv_wc *cqe, RdmaConnection *conn){
    RdmaRecvWr *wr = reinterpret_cast<RdmaRecvWr *>(cqe->wr_id);
    ibv_recv_wr *recv_wr = wr->get_ibv_recv_wr();

    if(conn == nullptr
        || conn->get_listener() == nullptr
        || cqe->opcode != IBV_WC_RECV
        || cqe->status != IBV_WC_SUCCESS
        || cqe->byte_len < sizeof(msg_cmd_t)){
        return 0;
    }

    msg_cmd_t *cmd = reinterpret_cast<msg_cmd_t *>(recv_wr->sg_list[0].addr);
    if(cmd->mh.cls != 0x30){
        //not msg module msg, ignore it.
        return 0;
    }

    ML(mct, debug, "len: {}, cls: {:x}, opcode: {}", 
                    cmd->mh.len, cmd->mh.cls, cmd->mh.opcode);

    if(cmd->mh.opcode == FLAME_MSG_HDR_OPCODE_DECLARE_ID){
        Msg *msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
        msg->type = FLAME_MSG_TYPE_DECLARE_ID;
        msg->append_data(cmd->content, sizeof(msg_cmd_delcared_id_t));
        conn->get_listener()->on_conn_recv(conn, msg);
        msg->put();
    }
    return 1;
}

void RdmaWorker::handle_rx_cqe(ibv_wc *cqe, int n){
    if(mct->config->rdma_conn_version == 2){
        std::map<RdmaConnection *, uint32_t> polled;
        for (int i = 0; i < n; ++i) {
            ibv_wc* response = &cqe[i];
            ML(mct, debug, "QP: {} len: {}, opcode: {}, wc_flags:{},"
                    " wr_id: {:x} {}", 
                    response->qp_num, response->byte_len, 
                    ib::Infiniband::wc_opcode_string(response->opcode), 
                    response->wc_flags, response->wr_id,
                    ib::Infiniband::wc_status_to_string(response->status));
            auto conn = get_rdma_conn(response->qp_num);

            if(polled.count(conn) > 0){
                ++polled[conn];
            }else{
                polled[conn] = 1;
            }

            RdmaRecvWr *wr = reinterpret_cast<RdmaRecvWr *>(response->wr_id);
            wr->get_ibv_recv_wr()->next = nullptr;
            //Got 0 byte msg, means close the conn.
            if(response->status == IBV_WC_SUCCESS
                && response->opcode == IBV_WC_RECV
                && response->byte_len == 0
                && !(response->wc_flags & IBV_WC_WITH_IMM)){
                conn->close_msg_arrive();
            }else if(!handle_rx_msg(response, conn)){
                //not msg module msg.
                wr->on_recv_done(conn, cqe[i]);
            }
            

            if (response->status != IBV_WC_SUCCESS
                && response->status != IBV_WC_WR_FLUSH_ERR){
                //Todo 
                //Not distinguish the error type here.
                //No need to close the conn for some error types.
                //But here always close.
                ML(mct, error, "work request returned error for wr_id({:x}) "
                                "status({}:{})", response->wr_id, 
                                response->status,
                        manager->get_ib().wc_status_to_string(response->status));
                if (conn)
                    conn->fault();
            }

            
        }

        if(!srq){
            std::vector<RdmaRecvWr *> recv_wrs;
            int rn = mct->manager->get_msger_cb()->get_recv_wrs(n, recv_wrs);
            assert(rn == n);
            uint32_t wrs_left = rn;
            for(auto &e : polled){
                assert(wrs_left > e.second);
                uint32_t begin = wrs_left - e.second;
                std::vector<RdmaRecvWr *> tmp_wrs(recv_wrs.begin()+begin, 
                                            recv_wrs.begin()+begin+e.second);
                e.first->post_recvs(tmp_wrs);
                wrs_left -= e.second;
            }
        }
       
        return;
    }
    std::map<RdmaConnection *, std::list<ibv_wc>> polled;
    for (int i = 0; i < n; ++i) {
        ibv_wc* response = &cqe[i];
        Chunk* chunk = reinterpret_cast<Chunk *>(response->wr_id);
        ML(mct, info, "QP: {} len: {}, opcode: {}, wc_flags:{}, addr: {:p} {}", 
                    response->qp_num, response->byte_len, 
                    ib::Infiniband::wc_opcode_string(response->opcode), 
                    response->wc_flags, (void *)chunk,
                    ib::Infiniband::wc_status_to_string(response->status));

        assert(response->opcode & IBV_WC_RECV);

        auto conn = get_rdma_conn(response->qp_num);

        if (response->status == IBV_WC_SUCCESS) {
            if (!conn) {
                ML(mct, info, "RdmaConnection with qpn {} may be dead. "
                                "Return rx buffer {:p} to MemoryManager.",
                                    response->qp_num, (void *)chunk);
                memory_manager->release_buffer(chunk);
            } else {
                polled[conn].push_back(*response);
            }
        }else if(response->status == IBV_WC_WR_FLUSH_ERR){
            //QP transitioned into Error State. Release all rx chunks.
            memory_manager->release_buffer(chunk);
        }else{
            //Todo 
            //Not distinguish the error type here.
            //No need to close the conn for some error types.
            //But here always close.
            ML(mct, error, "work request returned error for buffer({:p}) "
                            "status({}:{})", (void *)chunk, response->status,
                    manager->get_ib().wc_status_to_string(response->status));
            if (conn)
                conn->fault();

            memory_manager->release_buffer(chunk);
        }
    }
    for (auto &i : polled){
        i.first->pass_wc(i.second);
        i.first->recv_data();
        // i.first->read_cb();
        // i.first->recv_notify();
    }

}

int RdmaWorker::process_rx_cq_dry_run(){
    static int MAX_COMPLETIONS = RDMA_CQ_MAX_BATCH_COMPLETIONS;
    ibv_wc wc[MAX_COMPLETIONS];
    int total_proc = 0;
    int rx_ret = 0;

    while(true){
        //drain channel fd
        if(!rx_cc->get_cq_event()){
            break;
        }
    }

    rx_cq->rearm_notify();

    do{
        rx_ret = process_rx_cq(wc, MAX_COMPLETIONS);
        total_proc += rx_ret;
    }while(rx_ret > 0);

    return total_proc;
}

int RdmaWorker::process_rx_cq(ibv_wc *wc, int max_cqes){
    int rx_ret = rx_cq->poll_cq(max_cqes, wc);
    if(rx_ret > 0){
        ML(mct, info, "rx completion queue got {} responses.", rx_ret);
        if(mct->config->rdma_conn_version == 1){
            if(srq){
                srq_buffer_backlog = srq_buffer_backlog + rx_ret - 
                            post_chunks_to_rq(srq_buffer_backlog + rx_ret);
            }
        }else if(mct->config->rdma_conn_version == 2){
            if(srq){
                uint32_t i;
                for(i = 0;i < rx_ret;++i){
                    RdmaRecvWr *wr = 
                                reinterpret_cast<RdmaRecvWr *>(wc[i].wr_id);
                    if(inflight_recv_wrs.empty() 
                        || inflight_recv_wrs.front() != wr){
                        uint32_t it = 0;
                        bool found = false;
                        while(it < inflight_recv_wrs.size()){
                            if(inflight_recv_wrs[it] == wr){
                                ML(mct, warn, "wr's index is {}/{}, expect 0.", 
                                                it, inflight_recv_wrs.size());
                                inflight_recv_wrs.erase(
                                            inflight_recv_wrs.begin() + it);
                                found = true;
                                break;
                            }
                            ++it;
                        }
                        if(!found){
                            ML(mct, error, "wr not in inflight_recv_wrs!");
                        }
                    }else{
                        inflight_recv_wrs.pop_front();
                    }
                }

                uint32_t rx_queue_len = 
                                    get_manager()->get_ib().get_rx_queue_len();
                assert(rx_queue_len >= inflight_recv_wrs.size());
                post_rdma_recv_wr_to_srq(
                                    rx_queue_len - inflight_recv_wrs.size());
                
            }
        }
        
        //if don't have srq, reuse rx buffers after RdmaConn recv_data().
        handle_rx_cqe(wc, rx_ret);
        return rx_ret;
    }

    //here may be error or poll no cqe.
    return 0;
}

/**
 * @return: 0 nothing happend; 1 reg one; -1 unreg one;
 */
int RdmaWorker::reg_rdma_conn(uint32_t qpn, RdmaConnection *conn){
    if(conn){
        conn->get();
        conn->set_owner(owner);
    }
    auto it = qp_conns.find(qpn);
    if(it != qp_conns.end()){
        it->second->put();
        if(!conn){
            qp_conns.erase(it);
            return -1;
        }else{
            it->second = conn;
        }
    }else{
        if(conn){
            qp_conns[qpn] = conn;
        }else{
            return 0;
        }
    }
    return 1;

}

RdmaConnection *RdmaWorker::get_rdma_conn(uint32_t qpn){
    auto it = qp_conns.find(qpn);
    if(it != qp_conns.end()){
        return it->second;
    }
    return nullptr;
}

void RdmaWorker::make_conn_dead(RdmaConnection *conn){
    if(!conn) return;
    if(!get_owner()->am_self()){
        get_owner()->post_work([this, conn](){
            ML(this->mct, trace, "in make_conn_dead()");
            this->make_conn_dead(conn);
        });
        return;
    }
    if(conn->get_tx_wr() == 0 || conn->is_error()){
        conn->get_qp()->to_dead();
    }else{
        dead_conns.push_back(conn);
    }
    
}

int RdmaWorker::post_chunks_to_rq(std::vector<Chunk *> &chunks, ibv_qp *qp){
    if(srq){
        return manager->get_ib().post_chunks_to_srq(chunks, srq);
    }else{
        assert(qp);
        return manager->get_ib().post_chunks_to_rq(chunks, qp);
    }
}

int RdmaWorker::post_chunks_to_rq(int num, ibv_qp *qp){
    if(srq){
        return manager->get_ib().post_chunks_to_srq(num, srq);
    }else{
        assert(qp);
        return manager->get_ib().post_chunks_to_rq(num, qp);
    }
}

int RdmaWorker::post_rdma_recv_wr_to_srq(std::vector<RdmaRecvWr *> &wrs){
    if(!srq || wrs.empty() || mct->config->rdma_conn_version != 2 ) return -1;
    uint32_t i = 0;
    while(i + 1 < wrs.size()){
        assert(wrs[i] && wrs[i]->get_ibv_recv_wr());
        wrs[i]->get_ibv_recv_wr()->next = wrs[i+1]->get_ibv_recv_wr();
        ++i;
    }
    wrs[i]->get_ibv_recv_wr()->next = nullptr;
    ibv_recv_wr *recv_wr, *bad_recv_wr;
    recv_wr = wrs[0]->get_ibv_recv_wr();
    bad_recv_wr = nullptr;
    int ret = ibv_post_srq_recv(srq, recv_wr, &bad_recv_wr);
    assert(ret == 0);
    inflight_recv_wrs.insert(inflight_recv_wrs.end(), wrs.begin(), wrs.end());
    return wrs.size();
}

int RdmaWorker::post_rdma_recv_wr_to_srq(int n){
    if(!srq || n == 0 || mct->config->rdma_conn_version != 2 ) return -1;
    std::vector<RdmaRecvWr *> wrs;
    int rn = mct->manager->get_msger_cb()->get_recv_wrs(n, wrs);
    if(rn == 0) return 0;
    return post_rdma_recv_wr_to_srq(wrs); 
}

int RdmaWorker::on_buffer_reclaimed(){
    return 0;
}

int RdmaWorker::arm_notify(MsgWorker *worker){
    if(!worker || !mct->config->rdma_poll_event) return 1;
    tx_notifier = new RdmaTxCqNotifier(mct, this);
    tx_notifier->fd = tx_cc->get_fd();
    rx_notifier = new RdmaRxCqNotifier(mct, this);
    rx_notifier->fd = rx_cc->get_fd();

    int r = worker->add_event(tx_notifier);
    assert(r == 0);
    r = worker->add_event(rx_notifier);
    assert(r == 0);

    owner = worker;
    return 0;
}

int RdmaWorker::remove_notify(){
    if(tx_notifier){
        owner->del_event(tx_notifier->fd);
        tx_notifier->put();;
        tx_notifier = nullptr;
    }

    if(rx_notifier){
        owner->del_event(rx_notifier->fd);
        rx_notifier->put();
        rx_notifier = nullptr;
    }

    return 0;
}

int RdmaWorker::reg_poller(MsgWorker *worker){
    if(!worker || mct->config->rdma_poll_event) return 1;
    owner = worker;
    poller_id = worker->reg_poller([this]()->int{
        static int MAX_COMPLETIONS = RDMA_CQ_MAX_BATCH_COMPLETIONS;
        ibv_wc wc[MAX_COMPLETIONS];
        int total_proc = 0;
        total_proc += this->process_rx_cq(wc, MAX_COMPLETIONS);
        total_proc += this->process_tx_cq(wc, MAX_COMPLETIONS);
        this->reap_dead_conns();
        return total_proc;
    });
    return 0;
}

int RdmaWorker::unreg_poller(){
    if(mct->config->rdma_poll_event) return 1;
    owner->unreg_poller(poller_id);
    return 0;
}

static void clear_qp_fn(void *arg1, void *arg2){
    RdmaWorker  *worker = (RdmaWorker *)arg1;
    uint32_t qpn = reinterpret_cast<uint64_t>(arg2);
    worker->clear_qp(qpn);
}

int RdmaManager::handle_async_event(){
    auto d = get_ib().get_device();
    assert(d);
    int count = 0;
    ibv_async_event async_event;
    while(1){
        if(ibv_get_async_event(d->ctxt, &async_event)){
            if(errno != EAGAIN){
                ML(mct, error, "ibv_get_async_event failed. (errno= {})",
                                     cpp_strerror(errno));
                return -1;
            }
            break;
        }
        if(async_event.event_type == IBV_EVENT_QP_LAST_WQE_REACHED){
            ib::QueuePair *qp = ib::QueuePair::get_by_ibv_qp(
                                                        async_event.element.qp);
            RdmaConnection *rdma_conn = RdmaConnection::get_by_qp(qp);
            RdmaWorker *rdma_worker = rdma_conn->get_rdma_worker();

            uint32_t qpn = qp->get_local_qp_number();
            ML(mct, info, "event associated qp={} evt: {}, destroy it.", qpn,
                                    ibv_event_type_str(async_event.event_type));
            
            rdma_worker->get_owner()->post_work(clear_qp_fn,
                                                rdma_worker,
                                                reinterpret_cast<void *>(qpn));
            // for(auto &worker : workers){
            //     //Only one worker has the conn.
            //     //The others will do nothing.
            //     worker->get_owner()->post_work(clear_qp_fn,
            //                                     worker, 
            //                                     reinterpret_cast<void *>(qpn));
            // }

        }else{
            ML(mct, info, "ibv_get_async_event: dev={} evt: {}", 
                (void *)(d->ctxt), ibv_event_type_str(async_event.event_type));
        }
        ++count;
        ibv_ack_async_event(&async_event);
    }
    return count;
}

RdmaManager::~RdmaManager(){
    if(async_event_handler){
        owner->del_event(async_event_handler->fd);
        async_event_handler->put();
        async_event_handler = nullptr;
    }
    for(auto it : workers){
        if(!mct->config->rdma_poll_event){
            it->unreg_poller();
        }
        delete it;
    }
    workers.clear();
}

int RdmaManager::init(){
    int res = (m_ib.init()?0:1);
    if(res) return res;
    if(mct->manager->get_msger_cb()){
        mct->manager->get_msger_cb()->on_rdma_env_ready();
    }
    int msg_worker_num = mct->manager->get_worker_num();
    auto worker = mct->manager->get_worker(0); //use the first worker
    assert(worker);
    res = arm_async_event_handler(worker);
    if(res) return res;

    int cqp_num = mct->config->rdma_cq_pair_num;
    
    if(cqp_num >= msg_worker_num){
        cqp_num = msg_worker_num - 1;
        if(cqp_num == 0){
            cqp_num = 1;
        }
        ML(mct, warn, "config->rdma_cq_pair_num too large, "
                        "set equal to msg_worker_num - 1: {}", 
                        cqp_num);
    }
    assert(cqp_num > 0);
    workers.reserve(cqp_num);

    // int arm_step = (msg_worker_num - 1) / cqp_num;
    int arm_step = 1;
    ML(mct, info, "rdma_worker_num(rdma_cq_pair_num): {}, msg_worker_num: {},"
                        " arm_step:{}", cqp_num, msg_worker_num, arm_step);
    ML(mct, info, "poll mode: {}", 
                            mct->config->rdma_poll_event?"epoll":"direct poll");

    for(int i = 0;i < cqp_num; ++i){
        auto worker = new RdmaWorker(mct, this);
        worker->init();
        int msg_worker_index;
        if(msg_worker_num <= 1){
            msg_worker_index = 0;
        }else{
            msg_worker_index = (arm_step * i) % (msg_worker_num - 1) + 1;
        }
        MsgWorker *msg_worker = mct->manager->get_worker(msg_worker_index);
        ML(mct, info, "RdmaWorker{} bind to {}", i, msg_worker->get_name());
        if(mct->config->rdma_poll_event){
            res = worker->arm_notify(msg_worker);
            assert(res == 0);
        }else{
            res = worker->reg_poller(msg_worker);
            assert(res == 0);
        }
        workers.push_back(worker);
    }
    return res;
}

static void clear_worker_before_stop_fn(void *arg1, void *arg2){
    RdmaWorker *worker = (RdmaWorker *)arg1;
    worker->clear_before_stop();
}

int RdmaManager::clear_before_stop(){
    clear_done_worker_count = workers.size();
    for(auto it : workers){
        it->get_owner()->post_work(clear_worker_before_stop_fn,
                                    it, nullptr);
    }
    return 0;
}

void RdmaManager::worker_clear_done_notify(){
    int32_t cnt = --clear_done_worker_count;
    ML(mct, info, "clear_done_worker_cnt: {} -> {}", cnt + 1, cnt);
    if(clear_done_worker_count <= 0){
        mct->clear_done_notify();
    }
}

inline bool RdmaManager::is_clear_done(){
    return clear_done_worker_count <= 0;
}

RdmaWorker *RdmaManager::get_rdma_worker(int index){
    if(index < 0 || index >= workers.size()){
        return nullptr;
    }
    return workers[index];
}

RdmaWorker *RdmaManager::get_lightest_load_rdma_worker(){
    if(workers.size() <= 0) return nullptr;
    int load = workers[0]->get_qp_size(), index = 0;
    for(int i = 1;i < workers.size(); ++i){
        if(load > workers[i]->get_qp_size()){
            load = workers[i]->get_qp_size();
            index = i;
        }
    }
    return workers[index];
}

int RdmaManager::arm_async_event_handler(MsgWorker *worker){
    if(!worker) return 1;
    auto d = m_ib.get_device();
    assert(d);
    int res = d->set_async_fd_nonblock();
    if(res) return res;
    int fd = d->get_async_fd();

    async_event_handler = new RdmaAsyncEventHandler(mct, this);
    async_event_handler->fd = fd;

    res = worker->add_event(async_event_handler);
    if(!res){
        this->owner = worker;
    }
    return res;
}

RdmaStack::RdmaStack(MsgContext *c)
:mct(c), manager(nullptr), rdma_prep_conns_mutex(MUTEX_TYPE_ADAPTIVE_NP) {
    auto cfg = mct->config;
    assert(cfg != nullptr);
    max_msg_size_ = ((uint64_t)cfg->rdma_buffer_size) 
                        * cfg->rdma_send_queue_len;
}

int RdmaStack::init(){
    manager = new RdmaManager(mct);
    if(!manager) return -1;
    return manager->init();
}

int RdmaStack::clear_before_stop(){
    if(!manager) return 0;
    std::set<RdmaPrepConn *> tmp_set;
    {
        MutexLocker l(rdma_prep_conns_mutex);
        tmp_set.swap(alive_rdma_prep_conns);
    }
    for(auto prep_conn : tmp_set){
        prep_conn->put();
        prep_conn->close();
    }
    return manager->clear_before_stop();
}

inline bool RdmaStack::is_clear_done(){
    if(!manager) return true;
    return manager->is_clear_done();
}

int RdmaStack::fin(){
    delete manager;
    return 0;
}

ListenPort* RdmaStack::create_listen_port(NodeAddr *addr){
    return RdmaListenPort::create(mct, addr);
}

Connection* RdmaStack::connect(NodeAddr *addr){
    auto prep_conn = RdmaPrepConn::create(mct, addr);

    auto worker = mct->manager->get_lightest_load_worker();
    assert(worker);
    worker->add_event(prep_conn);
    prep_conn->set_owner(worker);

    Connection *conn = prep_conn->get_rdma_conn();
    //conn not only owned by prep_conn.
    conn->get();

    MutexLocker l(rdma_prep_conns_mutex);
    alive_rdma_prep_conns.insert(prep_conn);
    return conn;
}

ib::RdmaBufferAllocator *RdmaStack::get_rdma_allocator() {
    return manager->get_ib().get_memory_manager()->get_rdma_allocator();
}

void RdmaStack::on_rdma_prep_conn_close(RdmaPrepConn *conn){
    MutexLocker l(rdma_prep_conns_mutex);
    if(alive_rdma_prep_conns.erase(conn)){
        conn->put();
    }
}

} //namespace msg
} //namespace flame