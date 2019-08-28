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

void RdmaConnection::recv_msg_cb(Msg *msg){
    if(get_listener()){
        get_listener()->on_conn_recv(this, msg);
    }
    msg->put();
}

RdmaConnection::RdmaConnection(MsgContext *mct)
:Connection(mct),
 status(RdmaStatus::INIT),
 fin_msg_pending(false),
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
        chunk->prepare_read(response->byte_len);
        if(response->opcode == IBV_WC_RECV_RDMA_WITH_IMM
            || (response->opcode == IBV_WC_RECV
                && (response->wc_flags & IBV_WC_WITH_IMM))){
            Msg *msg = Msg::alloc_msg(mct, msg_ttype_t::RDMA);
            msg->type = FLAME_MSG_TYPE_IMM_DATA;
            msg->set_flags(FLAME_MSG_FLAG_RESP);
            msg->imm_data = ntohl(response->imm_data);
            ML(mct, debug, "recv imm_data: {}", msg->imm_data);
            recv_msg_cb(msg);
            chunks.push_back(chunk);
        }else if(response->byte_len == 0
                 && !(response->wc_flags & IBV_WC_WITH_IMM)){
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
            ML(mct, debug, "chunk length: {} bytes.  {:p}", response->byte_len, 
                                                        (void*)chunk);
            total += response->byte_len;
            decode_rx_buffer(chunk);
            chunks.push_back(chunk);
        }
        ++it;
    }

    // When disconnected, release rx buffers.
    if(status == RdmaStatus::CLOSING_PASSIVE
        || status == RdmaStatus::CLOSED
        || status == RdmaStatus::ERROR){ 
        rdma_worker->get_memory_manager()->release_buffers(chunks);
    }else if(qp->has_srq()){
        //release rx buffers 
        rdma_worker->get_memory_manager()->release_buffers(chunks);
    }else {
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

    return chunk->get_bound();
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

ssize_t RdmaConnection::send_msgs(std::list<Msg *> &msgs){
    if(status != RdmaStatus::INIT
        && status != RdmaStatus::CAN_WRITE){
        return -1;
    }

    for(auto msg : msgs){
         if(msg->get_data_len() > FLAME_MSG_CMD_RESERVED_LEN) return -1;

        auto send_wr = new InternalMsgSendWr(msg);
        assert(send_wr);
        post_send(send_wr, true);
    }
    post_send(nullptr, false);
    return 0;
}

ssize_t RdmaConnection::submit(bool more){
    if(status != RdmaStatus::CAN_WRITE){
        return 0;
    }
    int r = 0;

    r = post_imm_data(nullptr);
    if(r < 0){
        ML(mct, error, "post imm_data error!");
    }else{
        ML(mct, trace, "post imm_data: {}", r);
    }

    r = submit_rw_works();
    ML(mct, trace, "submit rw_works: {}", r);
    if(r < 0){
        ML(mct, error, "submit_rw_works error!");
    }

    r = submit_send_works();
    ML(mct, trace, "sumbit send_works: {}", r);
    if(r < 0){
        ML(mct, error, "submit_send_works error!");
    }

    return r;
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

    if(fin_msg_pending){
        this->fin(); //post fin msg if need
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
    uint32_t tx_queue_len = rdma_worker->get_manager()->get_ib()
                                                            .get_tx_queue_len();
    uint32_t max_wrs = (total_bytes + buf_size - 1) / buf_size;
    if(max_wrs == 0) return 0;

    //limit for tx_queue_len
    uint32_t can_post = qp->add_tx_wr_with_limit(max_wrs, tx_queue_len, true);

    if(can_post == 0) return 0;
    
    total_bytes = can_post * buf_size;

    memory_manager->get_buffers(total_bytes, chunks);
    if(chunks.size() < can_post){
        qp->dec_tx_wr(can_post - chunks.size());
        if(chunks.size() == 0){
            return 0;
        }
    }
    size_t chunk_bytes = chunks.size() * memory_manager->get_buffer_size();
    
    auto chunk_it = chunks.begin();
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

    size_t filled_chunks = std::distance(chunks.begin(), 
                                        (cur_chunk->get_offset() > 0)
                                            ?std::next(chunk_it)
                                            :chunk_it);
    if(chunks.size() > filled_chunks){
        qp->dec_tx_wr(chunks.size() - filled_chunks);
        std::vector<Chunk*> sub_chunks(chunks.begin()+filled_chunks,
                                                         chunks.end());
        memory_manager->release_buffers(sub_chunks);
        chunks.resize(filled_chunks);
    }

    size_t l = 0,  r = 1;
    for(;r < filled_chunks;++r){
        if((r - l) % RDMA_BATCH_SEND_WR_MAX == 0){
            auto b = chunks.begin();
            std::vector<Chunk*> sub_chunks(b + l, b + r);
            int result = post_work_request(sub_chunks);
            if(result < 0){
                return result;
            }
            l = r;
        }
    }

    if(r - l > 0){
        auto b = chunks.begin();
        std::vector<Chunk*> sub_chunks(b + l, chunks.end());
        int result = post_work_request(sub_chunks);
        if(result < 0){
            return result;
        }
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

int RdmaConnection::post_work_request(std::vector<Chunk *> &tx_buffers){
    // ML(mct, debug, "QP：{} {:p}", my_msg.qpn, (void *)tx_buffers.front());
    if(tx_buffers.size() == 0){
        return 0;
    }
    auto current_buffer = tx_buffers.begin();
    ibv_sge isge[tx_buffers.size()];
    uint32_t current_sge = 0;
    ibv_send_wr iswr[tx_buffers.size()];
    uint32_t current_swr = 0;
    ibv_send_wr* pre_wr = NULL;

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

        if (isge[current_sge].length < mct->config->rdma_max_inline_data) {
            iswr[current_swr].send_flags |= IBV_SEND_INLINE;
        }

        ML(mct, debug, "qp({}) sending buffer: {} length: {} {}", my_msg.qpn,
                (void *)(*current_buffer), isge[current_sge].length,
                (iswr[current_swr].send_flags & IBV_SEND_INLINE)?"inline":"" );

        if(pre_wr)
            pre_wr->next = &iswr[current_swr];
        pre_wr = &iswr[current_swr];
        ++current_sge;
        ++current_swr;
        ++current_buffer;
    }

    int r = tx_buffers.size();
    //qp->add_tx_wr_with_limit() ensure that send queue won't be full.
    ibv_send_wr *bad_tx_work_request = nullptr;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
            
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        r = -errno;
        if(bad_tx_work_request){
            uint32_t done_num = ((char *)bad_tx_work_request - (char *)iswr)
                                                        / sizeof(ibv_send_wr);
            assert(done_num <= tx_buffers.size());
            //some wrs not posted.
            qp->dec_tx_wr(tx_buffers.size() - done_num);
        }
    }
    // ML(mct, debug, "qp state: {}", 
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
    uint32_t wr_num = work->rbufs.size();
    if(wr_num > RDMA_RW_WORK_BUFS_LIMIT){
        ML(mct, error, "RdmaRwWork({:p}) has too much buffers. {} > {}", 
                                (void *)work, wr_num, RDMA_RW_WORK_BUFS_LIMIT);
        return -1;
    }
    uint32_t tx_queue_len = rdma_worker->get_manager()->get_ib()
                                                            .get_tx_queue_len();
    uint32_t can_post_wr = qp->add_tx_wr_with_limit(wr_num, tx_queue_len);
    if(can_post_wr < wr_num || status != RdmaStatus::CAN_WRITE){
        if(enqueue){
            MutexLocker l(send_mutex);
            rw_work_list.push_back(work);
        }
        return 0;
    }

    work->cnt = wr_num;

    //be careful that too large num may excceed the stack size.
    //here, one work's bufs is limited.
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
    ibv_send_wr *bad_tx_work_request = nullptr;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        r = -errno;
        if(bad_tx_work_request){
            uint32_t done_num = ((char *)bad_tx_work_request - (char *)iswr)
                                                        / sizeof(ibv_send_wr);
            assert(done_num <= wr_num);
            //some wrs not posted.
            qp->dec_tx_wr(wr_num - done_num);
        }
    }

    return r;
}

int RdmaConnection::post_imm_data(uint32_t imm_data){
    std::vector<uint32_t> imm_data_vec(1, imm_data);
    return post_imm_data(&imm_data_vec);
}

int RdmaConnection::post_imm_data(std::vector<uint32_t> *imm_data_vec){
    size_t wr_num = 0;
    std::deque<uint32_t> imm_data_to_send;
    {
        MutexLocker l(send_mutex);
        if(imm_data_vec){
            imm_data_list.insert(imm_data_list.end(), imm_data_vec->begin(),
                                                        imm_data_vec->end());
        }
        if(imm_data_list.size() > 0){
            imm_data_to_send.swap(imm_data_list);
        }
    }
    wr_num = std::min((uint32_t)imm_data_to_send.size(), 
                        RDMA_BATCH_SEND_WR_MAX);
    if(wr_num == 0){
        return 0;
    }
    uint32_t tx_queue_len = rdma_worker->get_manager()->get_ib()
                                                            .get_tx_queue_len();
    wr_num = qp->add_tx_wr_with_limit(wr_num, tx_queue_len, true);
    if(wr_num == 0 || status != RdmaStatus::CAN_WRITE){
        ML(mct, trace, "tx_queue may be full or qp not ready to send. Wait!");
        return 0;
    }

    ibv_send_wr iswr[wr_num];
    uint32_t current_swr = 0;
    ibv_send_wr* pre_wr = NULL;
    memset(iswr, 0, sizeof(iswr));

    while(current_swr < wr_num){
        iswr[current_swr].wr_id = 0;
        iswr[current_swr].next = nullptr;
        //Use WRITE_WITH_IMM for send the imm_data. According to
        //www.rdmamojo.com/2013/06/08/tips-and-tricks-to-optimize-your-rdma-code
        iswr[current_swr].opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        iswr[current_swr].imm_data = htonl(imm_data_to_send[current_swr]);
        ML(mct, debug, "send imm_data: {}", imm_data_to_send[current_swr]);
        if(pre_wr){
            pre_wr->next = &iswr[current_swr];
        }
        pre_wr = &iswr[current_swr];
        ++current_swr;
    }
    //only signal the last one
    iswr[current_swr - 1].send_flags |= IBV_SEND_SIGNALED; 
    //Use wrid to store the batch size.
    iswr[current_swr - 1].wr_id = sel_sig_wrid_from_num(current_swr); 

    size_t sended_num = wr_num;
    int r = sended_num;
    //qp->add_tx_wr_with_limit() ensure that send queue won't be full.
    ibv_send_wr *bad_tx_work_request = nullptr;
    if (ibv_post_send(qp->get_qp(), iswr, &bad_tx_work_request)) {
        if(errno == ENOMEM){
            ML(mct, error, "failed to send data. "
                        "(most probably send queue is full): {}",
                        cpp_strerror(errno));
            
        }else{
            ML(mct, error, "failed to send data. "
                        "(most probably should be peer not ready): {}",
                        cpp_strerror(errno));
        }
        r = -errno;
        if(bad_tx_work_request){
            uint32_t done_num = ((char *)bad_tx_work_request - (char *)iswr)
                                                        / sizeof(ibv_send_wr);
            assert(done_num <= wr_num);
            //some wrs not posted.
            qp->dec_tx_wr(wr_num - done_num);
            sended_num = done_num;
        }
    }
    // ML(mct, debug, "qp state: {}", 
    //                     ib::Infiniband::qp_state_string(qp->get_state()));
    if(imm_data_to_send.size() > sended_num){
        MutexLocker l(send_mutex);
        imm_data_list.insert(imm_data_list.begin(), 
                                imm_data_to_send.begin() + sended_num,
                                imm_data_to_send.end());
    }
    
    return r;
}



void RdmaConnection::fin(){
    uint32_t tx_queue_len = rdma_worker->get_manager()->get_ib()
                                                            .get_tx_queue_len();
    uint32_t can_post_wr = qp->add_tx_wr_with_limit(1, tx_queue_len);
    if(can_post_wr == 0){
        fin_msg_pending = true;
        return;
    }else{
        fin_msg_pending = false;
    }


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
        qp->dec_tx_wr(1);
        return ;
    }
    if(status != RdmaStatus::CLOSING_PASSIVE
        && status != RdmaStatus::CLOSED
        && status != RdmaStatus::ERROR){
        status = RdmaStatus::CLOSING_POSITIVE;
    }
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
