#include "Infiniband.h"
#include "RdmaConnection.h"
#include "RdmaStack.h"
#include "msg/internal/byteorder.h"
#include "msg/internal/node_addr.h"
#include "msg/internal/errno.h"

#include <algorithm>

namespace flame{

void RdmaConnection::read_cb(){
    // if(!this->get_owner()->am_self()){
    //     return;
    // }
    // ConnectionListener *listener = this->get_listener();
    // assert(listener != nullptr);
    // while(true){
    //     auto msg = this->recv_msg();
    //     if(msg){
    //         if(listener){
    //             listener->on_conn_recv(this, msg);
    //         }
    //         msg->put();
    //     }else{
    //         break;
    //     }
    // }
}

void RdmaConnection::write_cb(){
    this->submit(false);
}

void RdmaConnection::error_cb(){
    ML(mct, error, "RdmaConn {:p} status:{}", (void *)this, status_str(status));
    if(status == RdmaStatus::INIT){
        this->fault();
    }else if(status == RdmaStatus::CAN_WRITE){
        fin();
    }

}

void RdmaConnection::recv_msg_cb(Msg *msg){
    if(get_listener()){
        get_listener()->on_conn_recv(this, msg);
    }
    msg->put();
}


uint32_t RdmaConnection::max_post_works(){
    auto manager = rdma_worker->get_manager();
    uint32_t tx_queue_len = manager->get_ib().get_tx_queue_len();
    uint32_t concurrent_num = manager->max_concurrent_num;
    uint32_t cur_tx_queue_len = qp->get_tx_wr() + concurrent_num;
    assert(tx_queue_len >= qp->get_tx_wr());
    if(tx_queue_len > cur_tx_queue_len){
        return tx_queue_len - cur_tx_queue_len;
    }
    return 0;
}

RdmaConnection::RdmaConnection(MsgContext *mct)
:Connection(mct),
 status(RdmaStatus::INIT),
 is_dead_pending(false),
 send_mutex(MUTEX_TYPE_ADAPTIVE_NP),
 inflight_rx_buffers(0),
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
    my_msg.gid = ib.get_gid();

    w->reg_rdma_conn(my_msg.qpn, conn);
    if(!qp->has_srq()){
        auto required_rx_len = ib.get_rx_queue_len();
        auto actual_posted = w->post_chunks_to_rq(required_rx_len, 
                                                    qp->get_qp());
        if(actual_posted < required_rx_len){
            if(actual_posted > 0){
                ML(mct, warn, "required rx_buffers: {}, acutal get: {}",
                                        required_rx_len, actual_posted);
                conn->inflight_rx_buffers += actual_posted;
            }else{
                ML(mct, error, "!!! can't post rx buffers (no free buffers). "
                    "Create RdmaConn failed! "
                    "Please adjust buffer_limit and rx_queue_len, "
                    "or use more srq");
                conn->put();
                return nullptr;
            }
            
        }
    }

    return conn;
}

RdmaConnection::~RdmaConnection(){
    MLI(mct, info, "status: {}", status_str(status));
    if(!recv_msg_list.empty()){
        //clean the recv_msg_list.
        read_cb();
        if(!recv_msg_list.empty()){
            MLI(mct, warn, "recv_msg_list still not empty after read_cb()");
        }
    }

    if(qp){
        delete qp;
        qp = nullptr;
    }

    std::list<Msg *> msgs;
    std::list<RdmaRwWork *> rw_works;
    {
        MutexLocker l(send_mutex);
        msgs.swap(msg_list);
        rw_works.swap(rw_work_list);
    }

    for(auto work : rw_works){
        delete work;
    }

    for(auto msg : msgs){
        msg->put();
    }
    
    if(recv_cur_msg){
        recv_cur_msg->put();
        recv_cur_msg = nullptr;
    }


}

void RdmaConnection::pass_wc(std::list<ibv_wc> &wc){
    //MutexLocker l(recv_wc_mutex);
    recv_wc.splice(recv_wc.end(), wc);
}

void RdmaConnection::get_wc(std::list<ibv_wc> &wc){
    //MutexLocker l(recv_wc_mutex);
    wc.swap(recv_wc);
}

inline Msg *RdmaConnection::recv_msg(){
    recv_data();

    // if(!recv_msg_list.empty()){
    //     auto tmp = recv_msg_list.front();
    //     recv_msg_list.pop_front();
    //     return tmp;
    // }

    return nullptr;
}

size_t RdmaConnection::recv_data(){
    std::list<ibv_wc> cqe;
    get_wc(cqe);
    if(cqe.empty()){
        return 0;
    }

    ML(mct, info, "poll queue got {} responses. QP: {}", 
                                                        cqe.size(), my_msg.qpn);
    std::vector<Chunk *> chunks;
    bool got_close_msg = false;
    auto it = cqe.begin();
    size_t total = 0;
    while(it != cqe.end()){
        ibv_wc* response = &(*it);
        assert(response->status == IBV_WC_SUCCESS);
        Chunk* chunk = reinterpret_cast<Chunk *>(response->wr_id);
        ML(mct, debug, "chunk length: {} bytes.  {:p}", response->byte_len, 
                                                        (void*)chunk);
        chunk->prepare_read(response->byte_len);
        if(response->opcode == IBV_WC_RECV_RDMA_WITH_IMM){
            Msg *msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
            msg->type = FLAME_MSG_TYPE_IMM_DATA;
            msg->set_flags(FLAME_MSG_FLAG_RESP);
            msg->imm_data = ntohl(response->imm_data);
            recv_msg_cb(msg);
            chunks.push_back(chunk);
        }else if(response->byte_len == 0){
            if(!got_close_msg){
                got_close_msg = true;
                ML(mct, debug, "RdmaConn({}) got remote close msg...", 
                                                            (void *)this);
                if(status == RdmaStatus::CLOSING_POSITIVE){
                    status = RdmaStatus::CLOSED;
                }else if(status == RdmaStatus::INIT
                            || status == RdmaStatus::CAN_WRITE){
                    status = RdmaStatus::CLOSING_PASSIVE;
                }
            }
            rdma_worker->get_memory_manager()->release_buffer(chunk);
        }else{
            total += response->byte_len;
            decode_rx_buffer(chunk);
            chunks.push_back(chunk);
        }
        ++it;
    }

    // when disconnected, release all rx buffers.
    if(status == RdmaStatus::CLOSING_PASSIVE
        || status == RdmaStatus::CLOSED
        || status == RdmaStatus::ERROR){
        //release rx buffers 
        rdma_worker->get_memory_manager()->release_buffers(chunks);
    }else if(qp->has_srq()){
        //release rx buffers 
        rdma_worker->get_memory_manager()->release_buffers(chunks);
    }else{
        //return to self rq if no srq.
        rdma_worker->post_chunks_to_rq(chunks, qp->get_qp());
    }

    if(status == RdmaStatus::CLOSING_PASSIVE){
        this->close();
    }

    return total;
}

int RdmaConnection::decode_rx_buffer(ib::Chunk *chunk){
    uint32_t bytes = 0;
    // std::list<Msg *> msgs;
    MsgBuffer &recv_header_buffer = recv_cur_msg_header_buffer;
    auto &recv_offset = recv_cur_msg_offset;
    while(!chunk->over()){
        if(!recv_cur_msg){
            recv_cur_msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
            recv_header_buffer.clear();
            recv_offset = 0;
        }
        uint32_t recv_data_offset = recv_offset - sizeof(flame_msg_header_t);

        if(!recv_header_buffer.full()){
            bytes = chunk->read(recv_header_buffer.data()
                                    + recv_header_buffer.offset(),
                                recv_header_buffer.length()
                                    - recv_header_buffer.offset());
            
            recv_header_buffer.advance(bytes);
            if(recv_header_buffer.full()){
                ssize_t r = recv_cur_msg->decode_header(recv_header_buffer);
                assert(r > 0);
            }
        }else if(recv_data_offset < recv_cur_msg->data_len){
            auto need_data_len = recv_cur_msg->data_len - recv_data_offset;
            auto avail_bytes = chunk->get_bound() - chunk->get_offset();
            bytes = std::min(need_data_len, avail_bytes);

            recv_cur_msg->append_data(chunk->data + chunk->get_offset(), bytes);

            chunk->set_offset(bytes + chunk->get_offset());
        }
        
        if(recv_header_buffer.full() 
            && recv_cur_msg->data_len == recv_cur_msg->get_data_len()){
            recv_msg_cb(recv_cur_msg);
            //msgs.push_back(recv_cur_msg);
            recv_cur_msg = nullptr;
        }

        recv_offset += bytes;
    }

    // recv_msg_list.splice(recv_msg_list.end(), msgs);

    return chunk->get_bound();
}

ssize_t RdmaConnection::send_msg(Msg *msg){
    if(status != RdmaStatus::INIT
        && status != RdmaStatus::CAN_WRITE){
        return -1;
    }

    if(msg){
        msg->get();
        MutexLocker l(send_mutex);
        msg_list.push_back(msg);
    }

    submit(false); //submit all rw_works and msgs.
    
    return 0;
}

ssize_t RdmaConnection::send_msgs(std::list<Msg *> &msgs){
    if(status != RdmaStatus::INIT
        && status != RdmaStatus::CAN_WRITE){
        return -1;
    }

    for(auto msg : msgs){
        msg->get();
    }

    {
        MutexLocker l(send_mutex);
        msg_list.splice(msg_list.end(), msgs);
    }

    submit(false); //submit all rw_works and msgs.
    
    return 0;
}

ssize_t RdmaConnection::submit(bool more){
    if(status != RdmaStatus::CAN_WRITE){
        return 0;
    }
    int r = 0;
    r = submit_rw_works();
    if(r < 0){
        ML(mct, error, "submit_rw_works error!");
        return r;
    }
    r = submit_send_works();
    if(r < 0){
        ML(mct, error, "submit_send_works error!");
    }
    return 0;
}
    
int RdmaConnection::submit_send_works(){
    std::list<Msg *> to_submit_msg_list;
    {
        MutexLocker l(send_mutex);
        to_submit_msg_list.swap(msg_list);
    }

    if(to_submit_msg_list.empty()){
        return 0;
    }
    
    int r = post_rdma_send(to_submit_msg_list);

    if(!to_submit_msg_list.empty()){
        MutexLocker l(send_mutex);
        msg_list.splice(msg_list.begin(), to_submit_msg_list);
    }

    return r;
}

int RdmaConnection::post_rdma_send(std::list<Msg*> &msgs){
    size_t total_bytes = 0;
    int cnt = 0;
    for(auto m : msgs){
        total_bytes += m->total_bytes();
    }
    std::vector<Chunk*> chunks;
    MsgBuffer msg_header_buffer(FLAME_MSG_HEADER_LEN);

    auto memory_manager = rdma_worker->get_memory_manager();
    uint32_t buf_size = memory_manager->get_buffer_size();
    uint32_t max_wrs = max_post_works();
    if(max_wrs * buf_size < total_bytes){ //limit works num.
        total_bytes = max_wrs * buf_size;
    }

    if(total_bytes == 0) return 0;

    memory_manager->get_buffers(total_bytes, chunks);
    size_t chunk_bytes = chunks.size() * memory_manager->get_buffer_size();
    
    auto chunk_it = chunks.begin();
    if(chunk_it == chunks.end()){
        return 0;
    }
    Chunk *cur_chunk = *chunk_it;
    cur_chunk->clear();
    while(!msgs.empty()){
        Msg *msg = msgs.front();
        if(msg->total_bytes() > chunk_bytes){
            break;
        }
        ssize_t _r = msg->encode_header(msg_header_buffer);
        assert(_r > 0);
        auto msg_data_iter = msg->data_iter();

        size_t msg_offset = 0;
        // write header
        while(msg_offset < FLAME_MSG_HEADER_LEN){
            msg_offset += 
                cur_chunk->write(msg_header_buffer.data() + msg_offset,
                                    msg_header_buffer.offset() - msg_offset);
            if(cur_chunk->full()){
                ++chunk_it;
                assert(chunk_it != chunks.end());
                cur_chunk = *chunk_it;
                cur_chunk->clear();
            }
        }

        // wirte data
        while(msg_offset < msg->total_bytes()){
            int cb_len = 0;
            char *buf = msg_data_iter.cur_data_buffer(cb_len);
            assert(buf != nullptr);
            int r = cur_chunk->write(buf, cb_len);
            msg_offset += r;
            msg_data_iter.cur_data_buffer_extend(r);
            if(cur_chunk->full()){
                ++chunk_it;
                assert(chunk_it != chunks.end());
                cur_chunk = *chunk_it;
                cur_chunk->clear();
            }
        }

        chunk_bytes -= msg_offset;

        msgs.pop_front();
        msg->put();
        ++cnt;
    }

    int r = post_work_request(chunks);
    if(r < 0){
        return r;
    }

    return cnt;
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
    qpa.max_dest_rd_atomic = 1;
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
    qpa.max_rd_atomic = 1;

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
    this->submit(false); // trigger submit
    return 0;
}

int RdmaConnection::post_work_request(std::vector<Chunk *> &tx_buffers){
    // ML(mct, debug, "QP：{} {:p}", my_msg.qpn, (void *)tx_buffers.front());
    auto current_buffer = tx_buffers.begin();
    ibv_sge isge[tx_buffers.size()];
    uint32_t current_sge = 0;
    ibv_send_wr iswr[tx_buffers.size()];
    uint32_t current_swr = 0;
    ibv_send_wr* pre_wr = NULL;
    uint32_t num = 0; 

    memset(iswr, 0, sizeof(iswr));
    memset(isge, 0, sizeof(isge));
    
    while (current_buffer != tx_buffers.end()) {
        isge[current_sge].addr = reinterpret_cast<uint64_t>
                                                    ((*current_buffer)->data);
        isge[current_sge].length = (*current_buffer)->get_offset();
        isge[current_sge].lkey = (*current_buffer)->lkey;

        iswr[current_swr].wr_id = reinterpret_cast<uint64_t>(*current_buffer);
        iswr[current_swr].next = NULL;
        iswr[current_swr].sg_list = &isge[current_sge];
        iswr[current_swr].num_sge = 1;
        iswr[current_swr].opcode = IBV_WR_SEND;
        iswr[current_swr].send_flags |= IBV_SEND_SIGNALED;

        if (isge[current_sge].length < ib::Infiniband::get_max_inline_data()) {
            iswr[current_swr].send_flags |= IBV_SEND_INLINE;
        }

        ML(mct, debug, "sending buffer: {} length: {} {}", 
                (void *)(*current_buffer), isge[current_sge].length,
                (iswr[current_swr].send_flags & IBV_SEND_INLINE)?"inline":"" );

        num++;
        if(pre_wr)
            pre_wr->next = &iswr[current_swr];
        pre_wr = &iswr[current_swr];
        ++current_sge;
        ++current_swr;
        ++current_buffer;
    }

    int r = 0;
    //can_post_works() ensure that send queue won't be full.
    ibv_send_wr *bad_tx_work_request;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
            num = ((char *)bad_tx_work_request - (char *)iswr)
                                                     / sizeof(ibv_send_wr);
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        r = -errno;
    }
    qp->add_tx_wr(num);
    // ML(mct, debug, "qp state is {}", 
    //                     ib::Infiniband::qp_state_string(qp->get_state()));
    return r;
}

int RdmaConnection::submit_rw_works(){
    int r = 0;
    std::list<RdmaRwWork *> work_list;
    {
        MutexLocker l(send_mutex);
        work_list.swap(rw_work_list);
    }

    while(!work_list.empty()){
        r = post_rdma_rw(work_list.front(), false);
        if(r < 0){
            break;
        }
        work_list.pop_front();
    }

    if(!work_list.empty()){
        MutexLocker l(send_mutex);
        rw_work_list.splice(rw_work_list.begin(), work_list);
    }
    return r;
}

int RdmaConnection::post_rdma_rw(RdmaRwWork *work, bool enqueue){
    if(!work) return -1;
    int wr_num = work->rbufs.size();
    if(max_post_works() < wr_num || status != RdmaStatus::CAN_WRITE){
        if(enqueue){
            MutexLocker l(send_mutex);
            rw_work_list.push_back(work);
        }
        return -1;
    }

    work->cnt = wr_num;

    ibv_sge isge[wr_num];
    uint32_t current_sge = 0;
    ibv_send_wr iswr[wr_num];
    uint32_t current_swr = 0;
    ibv_send_wr* pre_wr = NULL;
    uint32_t num = 0; 

    memset(iswr, 0, sizeof(iswr));
    memset(isge, 0, sizeof(isge));

    while(num < wr_num){
        isge[current_sge].addr = work->lbufs[num]->addr();
        if(work->is_write){
            isge[current_sge].length = work->lbufs[num]->data_len;
            work->rbufs[num]->data_len = work->lbufs[num]->data_len;
        }else{
            isge[current_sge].length = work->rbufs[num]->data_len;
            work->lbufs[num]->data_len = work->rbufs[num]->data_len;
        }
        
        isge[current_sge].lkey = work->lbufs[num]->lkey();
        ML(mct, debug, "{} rbuffer: {:x} length: {}",  
                                            work->is_write?"write":"read", 
                                            work->rbufs[num]->addr(), 
                                            work->rbufs[num]->data_len);

        //use work pointer as wr_id.
        iswr[current_swr].wr_id = reinterpret_cast<uint64_t>(work);
        iswr[current_swr].next = NULL;
        iswr[current_swr].sg_list = &isge[current_sge];
        iswr[current_swr].num_sge = 1;
        iswr[current_swr].opcode = 
                        work->is_write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
        iswr[current_swr].send_flags |= IBV_SEND_SIGNALED;

        iswr[current_swr].wr.rdma.remote_addr = work->rbufs[num]->addr();
        iswr[current_swr].wr.rdma.rkey = work->rbufs[num]->rkey();

        ++num;
        if(pre_wr)
            pre_wr->next = &iswr[current_swr];
        pre_wr = &iswr[current_swr];
        ++current_sge;
        ++current_swr;
    }

    if(work->is_write && work->imm_data != 0){
        //the last wr use write_with_imm when imm_data is not 0.
        iswr[current_swr - 1].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        iswr[current_swr - 1].imm_data = htonl(work->imm_data);
    }

    int r = 0;
    //can_post_works() ensure that send queue won't be full.
    ibv_send_wr *bad_tx_work_request;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
            num = ((char *)bad_tx_work_request - (char *)iswr)
                                                     / sizeof(ibv_send_wr);
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        r = -errno;
    }
    qp->add_tx_wr(num);

    return r;
}


void RdmaConnection::fin(){
    ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = reinterpret_cast<uint64_t>(qp);
    wr.num_sge = 0;
    wr.opcode = IBV_WR_SEND;
    wr.send_flags |= IBV_SEND_SIGNALED;
    ibv_send_wr* bad_tx_work_request;
    if (ibv_post_send(qp->get_qp(), &wr, &bad_tx_work_request)) {
        ML(mct, warn, "failed to send fin message. "
            "ibv_post_send failed(most probably should be peer not ready): {}",
            cpp_strerror(errno));
        return ;
    }
    qp->add_tx_wr(1);
    if(status != RdmaStatus::CLOSING_PASSIVE
        && status != RdmaStatus::CLOSED
        && status != RdmaStatus::ERROR){
        status = RdmaStatus::CLOSING_POSITIVE;
    }
}

void RdmaConnection::close(){
    if(status == RdmaStatus::CLOSED
        && status == RdmaStatus::ERROR){
        return;
    }
    if(status == RdmaStatus::CAN_WRITE){
        fin();
    }
    this->get_listener()->on_conn_error(this);
    if(is_dead_pending){
        return;
    }
    is_dead_pending = true;
    rdma_worker->make_conn_dead(this);
    
}

void RdmaConnection::fault(){
    status = RdmaStatus::ERROR;
    this->get_listener()->on_conn_error(this);
    if(is_dead_pending){
        return;
    }
    is_dead_pending = true;
    rdma_worker->make_conn_dead(this);
    
}


}