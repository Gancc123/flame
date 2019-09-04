/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 16:45:20
 */
#ifndef FLAME_TESTS_MSG_RDMA_CONN_V2_MSGER_H
#define FLAME_TESTS_MSG_RDMA_CONN_V2_MSGER_H

#include "msg/msg_core.h"
#include "common/thread/mutex.h"
#include "include/buffer.h"

#include <deque>
#include <sys/queue.h>

namespace flame {
namespace msg{

class RwRequestPool;
class RwMsger;

struct memory_info{
    uint32_t rkey; 
    uint64_t addr;
} __attribute__((packed));

class RwRequest : public RdmaRecvWr, public RdmaSendWr{
public:
    enum Status{
        FREE = 0,
        RECV_DONE,
        EXEC_DONE,
        SEND_DONE,
        DESTROY,
        ERROR,
    };
private:
    MsgContext *mct;
    RwMsger *msger;
    ibv_sge sge;
    ibv_send_wr send_wr;
    ibv_recv_wr recv_wr;
    Buffer *buf;
    RwRequest(MsgContext *c, RwMsger *m)
    : mct(c), msger(m), status(FREE), conn(nullptr) {}
public:
    Status status;
    RdmaConnection *conn;
    memory_info *data;

    static RwRequest* create(MsgContext *c, RwMsger *m);

    ~RwRequest();

    virtual ibv_send_wr *get_ibv_send_wr() override{
        return &send_wr;
    }

    virtual ibv_recv_wr *get_ibv_recv_wr() override{
        return &recv_wr;
    }

    virtual void on_send_done(ibv_wc &cqe) override;

    virtual void on_send_cancelled(bool err, int eno=0) override;

    virtual void on_recv_done(RdmaConnection *conn, ibv_wc &cqe) override;

    virtual void on_recv_cancelled(bool err, int eno=0) override;

    void run();

};//class RwRequest

class RwRequestPool{
    MsgContext *mct;
    RwMsger *msger;
    std::deque<RwRequest *> reqs_free;
    Mutex mutex_;
    int expand_lockfree(int n);
    int purge_lockfree(int n);
public:
    explicit RwRequestPool(MsgContext *c, RwMsger *m);
    ~RwRequestPool();

    int alloc_reqs(int n, std::vector<RwRequest *> &reqs);
    RwRequest *alloc_req();
    void free_req(RwRequest *req);

    int expand(int n);
    int purge(int n);
};//class RwRequestPool


class RwMsger : public MsgerCallback{
    MsgContext *mct;
    RwRequestPool pool;
    bool is_server_;
public:
    explicit RwMsger(MsgContext *c, bool s) 
    : mct(c), pool(c, this), is_server_(s) {};
    virtual void on_conn_declared(Connection *conn, Session *s) override;
    virtual void on_conn_recv(Connection *conn, Msg *msg) override {};
    virtual int get_recv_wrs(int n, std::vector<RdmaRecvWr *> &wrs) override;

    RwRequestPool &get_req_pool() { return pool; }
    bool is_server() { return is_server_; }
};//class RwMsger

} //namespace msg
} //namespace flame

#endif //FLAME_TESTS_MSG_RDMA_CONN_V2_MSGER_H