/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 18:13:35
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

class RequestPool;
class Msger;

struct test_data_t{
    uint32_t ignore; // ignore a hdr cmd
    uint32_t count;
    uint64_t raddr;
    uint32_t rkey;
    uint32_t length;
    uint8_t  is_read;
} __attribute__((packed));

class Request : public RdmaRecvWr, public RdmaSendWr{
public:
    enum Status{
        FREE = 0,
        RECV_DONE,
        READ_DONE,
        WRITE_DONE,
        EXEC_DONE,
        SEND_DONE,
        DESTROY,
        ERROR,
    };
private:
    using RdmaBuffer = flame::memory::ib::RdmaBuffer;
    MsgContext *mct;
    Msger *msger;
    ibv_sge sge;
    ibv_sge data_sge;
    ibv_send_wr send_wr;
    ibv_recv_wr recv_wr;
    Buffer *buf;
    Buffer *data_buffer;
    Request(MsgContext *c, Msger *m)
    : mct(c), msger(m), status(FREE), conn(nullptr) {}
public:
    Status status;
    RdmaConnection *conn;
    test_data_t *data;

    static Request* create(MsgContext *c, Msger *m);

    ~Request();

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

};//class Request

class RequestPool{
    MsgContext *mct;
    Msger *msger;
    std::deque<Request *> reqs_free;
    Mutex mutex_;
    int expand_lockfree(int n);
    int purge_lockfree(int n);
public:
    explicit RequestPool(MsgContext *c, Msger *m);
    ~RequestPool();

    int alloc_reqs(int n, std::vector<Request *> &reqs);
    Request *alloc_req();
    void free_req(Request *req);

    int expand(int n);
    int purge(int n);
};//class RequestPool


class Msger : public MsgerCallback{
    MsgContext *mct;
    RequestPool pool;
    bool is_server_;
public:
    explicit Msger(MsgContext *c, bool s) 
    : mct(c), pool(c, this), is_server_(s) {};
    virtual void on_conn_declared(Connection *conn, Session *s) override;
    virtual void on_conn_recv(Connection *conn, Msg *msg) override {};
    virtual int get_recv_wrs(int n, std::vector<RdmaRecvWr *> &wrs) override;
    virtual void on_rdma_env_ready() override;

    RequestPool &get_req_pool() { return pool; }
    bool is_server() { return is_server_; }
};//class Msger

} //namespace msg
} //namespace flame

#endif //FLAME_TESTS_MSG_RDMA_CONN_V2_MSGER_H