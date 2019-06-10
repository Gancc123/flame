#ifndef FLAME_MSG_RDMA_RDMA_STACK_H
#define FLAME_MSG_RDMA_RDMA_STACK_H

#include "msg/Stack.h"
#include "msg/event/event.h"
#include "msg/MsgWorker.h"
#include "common/thread/mutex.h"
#include "common/thread/cond.h"
#include "msg/msg_context.h"
#include "RdmaConnection.h"
#include "Infiniband.h"
#include "RdmaMem.h"

#include <map>
#include <vector>
#include <deque>
#include <set>
#include <functional>

namespace flame{
namespace msg{

/**
 * ibv_send_wr/ibv_recv_wr->wr_id must be the pointer to RdmaSendWr/RdmaRecvWr
 */
class RdmaSendWr{
public:
    virtual ibv_send_wr* get_ibv_send_wr() = 0;
    /**
     * When cqe generated, call this func.
     * cqe.status maybe IBV_WC_SUCCESS or other errors.
     */
    virtual void on_send_done(ibv_wc &cqe) = 0;
    /**
     * When ibv_post_send() failed or conn is closed, call this func.
     * When ibv_post_send() failed, err will be true, and eno is the errno.
     * When conn is closed, err will be false.
     */
    virtual void on_send_cancelled(bool err, int eno=0) = 0;

    virtual ~RdmaSendWr() {}
};

class RdmaRecvWr{
public:
    virtual ibv_recv_wr* get_ibv_recv_wr() = 0;
     /**
     * When cqe generated, call this func.
     * cqe.status maybe IBV_WC_SUCCESS or other errors.
     */
    virtual void on_recv_done(RdmaConnection *conn, ibv_wc &cqe) = 0;
    /**
     * When ibv_post_recv()/ibv_srq_post_recv() failed or conn is closed, 
     * call this func.
     * 
     * When ibv_post_recv()/ibv_srq_post_recv() failed, err will be true,
     *  and eno is the errno.
     * When conn is closed, err will be false.
     */
    virtual void on_recv_cancelled(bool err, int eno=0) = 0;

    virtual ~RdmaRecvWr() {}
};

class RdmaWorker;
class RdmaManager;
class RdmaPrepConn;

struct RdmaRwWork;
typedef std::function<void(RdmaRwWork *, RdmaConnection*)> rdma_rw_work_func_t;

struct RdmaRwWork{
    using RdmaBuffer = ib::RdmaBuffer;
    std::vector<RdmaBuffer *> rbufs; //bufs num should <= 8.
    std::vector<RdmaBuffer *> lbufs; //must be same num as rbufs.
    bool is_write;
    uint32_t imm_data = 0; // 0 means no imm data.
    int cnt;
    std::vector<int> failed_indexes;
    rdma_rw_work_func_t target_func = nullptr;

    virtual ~RdmaRwWork() {}

    virtual void callback(RdmaConnection *conn){
        if(target_func){
            target_func(this, conn);
        }
    }
};

class RdmaTxCqNotifier : public EventCallBack{
    RdmaWorker *worker;
public:
    explicit RdmaTxCqNotifier(MsgContext *mct, RdmaWorker *w)
    :EventCallBack(mct, FLAME_EVENT_READABLE), worker(w){}
    virtual void read_cb() override;
};

class RdmaRxCqNotifier : public EventCallBack{
    RdmaWorker *worker;
public:
    explicit RdmaRxCqNotifier(MsgContext *mct, RdmaWorker *w)
    :EventCallBack(mct, FLAME_EVENT_READABLE), worker(w){}
    virtual void read_cb() override;
};

class RdmaAsyncEventHandler : public EventCallBack{
    RdmaManager *manager;
public:
    explicit RdmaAsyncEventHandler(MsgContext *mct, RdmaManager *m)
    :EventCallBack(mct, FLAME_EVENT_READABLE), manager(m){}
    virtual void read_cb() override;
};


class RdmaWorker{
    using Chunk = ib::Chunk; 
    MsgContext *mct;
    MsgWorker *owner = nullptr;
    uint64_t poller_id = 0;
    RdmaManager *manager;
    ib::MemoryManager *memory_manager = nullptr;
    ib::CompletionChannel *tx_cc = nullptr, *rx_cc = nullptr;
    ib::CompletionQueue   *tx_cq = nullptr, *rx_cq = nullptr;
    RdmaTxCqNotifier *tx_notifier = nullptr;
    RdmaRxCqNotifier *rx_notifier = nullptr;
    ibv_srq *srq = nullptr; // if srq enabled, one worker has one srq.
    uint32_t srq_buffer_backlog = 0;
    std::deque<RdmaRecvWr *> inflight_recv_wrs;

    std::map<uint32_t , RdmaConnection *> qp_conns; // qpn, conn

    //Waitting for dead connection that has no tx_cq to poll.
    //This will ensure that all tx_buffer will be returned to memory manager.
    std::list<RdmaConnection *> dead_conns;

    std::atomic<bool> is_fin;

    void handle_tx_cqe(ibv_wc *cqe, int n);
    void handle_rdma_rw_cqe(ibv_wc &wc, RdmaConnection *conn);
    void handle_rx_cqe(ibv_wc *cqe, int n);
    int handle_rx_msg(ibv_wc *cqe, RdmaConnection *conn);
public:
    explicit RdmaWorker(MsgContext *c, RdmaManager *m)
    :mct(c), manager(m) {}
    ~RdmaWorker();
    int init();
    int clear_before_stop();
    void clear_qp(uint32_t qpn);
    int on_buffer_reclaimed();
    int arm_notify(MsgWorker *worker);
    int remove_notify();
    int reg_poller(MsgWorker *worker);
    int unreg_poller();
    void reap_dead_conns();
    int process_cq_dry_run();
    int process_tx_cq(ibv_wc *wc, int max_cqes);
    int process_tx_cq_dry_run();
    int process_rx_cq(ibv_wc *wc, int max_cqes);
    int process_rx_cq_dry_run();
    int reg_rdma_conn(uint32_t qpn, RdmaConnection *conn);
    RdmaConnection *get_rdma_conn(uint32_t qpn);
    void make_conn_dead(RdmaConnection *conn);
    int post_chunks_to_rq(std::vector<Chunk *> &chunks, ibv_qp *qp=nullptr);
    int post_chunks_to_rq(int num, ibv_qp *qp=nullptr);
    int post_rdma_recv_wr_to_srq(std::vector<RdmaRecvWr *> &wrs);
    int post_rdma_recv_wr_to_srq(int n);
    ibv_srq *get_srq() const { return this->srq; }
    ib::CompletionQueue *get_tx_cq() const { return tx_cq; }
    ib::CompletionQueue *get_rx_cq() const { return rx_cq; }
    int get_qp_size() const { return qp_conns.size(); }
    MsgWorker *get_owner() const { return this->owner; }
    RdmaManager *get_manager() const { return this->manager; }
    ib::MemoryManager *get_memory_manager() const { 
        return this->memory_manager; 
    }
};

class RdmaManager{
    using Chunk = ib::Chunk; 
    MsgContext *mct;
    ib::Infiniband m_ib;
    MsgWorker *owner = nullptr;
    RdmaAsyncEventHandler *async_event_handler = nullptr;

    std::vector<RdmaWorker *> workers;
    std::atomic<int32_t> clear_done_worker_count;
public:
    explicit RdmaManager(MsgContext *c)
    :mct(c), m_ib(c), clear_done_worker_count(0) {};
    ~RdmaManager();
    int init();
    int clear_before_stop();
    void worker_clear_done_notify();
    bool is_clear_done();
    int arm_async_event_handler(MsgWorker *worker);
    int handle_async_event();
    int get_rdma_worker_num() { return workers.size(); }
    RdmaWorker *get_rdma_worker(int index);
    RdmaWorker *get_lightest_load_rdma_worker();
    MsgWorker *get_owner() const { return this->owner; }
    ib::Infiniband &get_ib() { return m_ib; }

};


class RdmaStack : public Stack{
    MsgContext *mct;
    RdmaManager *manager;
    Mutex rdma_prep_conns_mutex;
    std::set<RdmaPrepConn *> alive_rdma_prep_conns;
    uint64_t max_msg_size_;
public:
    explicit RdmaStack(MsgContext *c);
    virtual int init() override;
    virtual int clear_before_stop() override;
    virtual bool is_clear_done() override;
    virtual int fin() override;
    virtual ListenPort* create_listen_port(NodeAddr *addr) override;
    virtual Connection* connect(NodeAddr *addr) override;
    RdmaManager *get_manager() { return manager; }
    ib::RdmaBufferAllocator *get_rdma_allocator();
    static RdmaConnection *rdma_conn_cast(Connection *conn){
        auto ttype = conn->get_ttype();
        if(ttype == msg_ttype_t::RDMA){
            return static_cast<RdmaConnection *>(conn);
        }
        return nullptr;
    }
    uint64_t max_msg_size(){
        return max_msg_size_;
    }
    void on_rdma_prep_conn_close(RdmaPrepConn *prep_conn);
};


} //namespace msg
} //namespace flame

#endif