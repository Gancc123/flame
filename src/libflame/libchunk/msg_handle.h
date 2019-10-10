/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:02:43
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-29 15:57:53
 */
#ifndef FLAME_LIBFLAME_LIBCHUNK_MSG_HANDLE_H
#define FLAME_LIBFLAME_LIBCHUNK_MSG_HANDLE_H

#include "msg/msg_core.h"
#include "include/cmd.h"
#include "common/thread/mutex.h"
#include "common/context.h"
#include "common/log.h"
#include "memzone/rdma_mz.h"

#include <deque>
#include <sys/queue.h>

namespace flame {

class RequestPool;
class Msger;
//----------------RdmaWorkRequest----------------------------//
class RdmaWorkRequest : public msg::RdmaRecvWr, public msg::RdmaSendWr{
public:
    enum Status{
        FREE = 0,
        RECV_DONE,          //**接收到SEND消息
        EXEC_DONE,          //**处理完成包括两种：1.数据从disk到lbuf；2.数据从lbuf到disk
        SEND_DONE,          //**发送完SEND消息
        READ_DONE,          //**READ完
        WRITE_DONE,         //**WRITE完
        DESTROY,            //**request被销毁
        WAIT,               //**前后涉及两个req，有相互等待的问题，如第二个req进行RDMA write时，第一个req闲置
        ERROR,              //**出错
    };
private:
    msg::MsgContext *msg_context_;
    Msger* msger_;
    ibv_sge sge_[2];
    ibv_send_wr send_wr_;
    ibv_recv_wr recv_wr_;
    Buffer* buf_[2];
    Buffer* data_buf_;
    CmdService* service_;
    RdmaWorkRequest(msg::MsgContext *c, Msger *m)
    : msg_context_(c), msger_(m), status(FREE), conn(nullptr){}
    static int allocate_send_buffer(RdmaWorkRequest* req);
    static int prepare_send_recv(RdmaWorkRequest* req);
    static int set_command(RdmaWorkRequest* req, void* addr);
    uint32_t _get_command_queue_n();
    bool _judge_seq_type(int type);
public:
    ~RdmaWorkRequest();
    Status status;
    msg::RdmaConnection *conn;
    void *command;
    static RdmaWorkRequest* create_request(msg::MsgContext *c, Msger *m);//创建默认的send/recv request，如果要write or read，则需要额外设置
    inline virtual ibv_send_wr *get_ibv_send_wr() override{
        return &send_wr_;
    }
    inline virtual ibv_recv_wr *get_ibv_recv_wr() override{
        return &recv_wr_;
    }
    inline virtual Buffer* get_data_buf(){
        return data_buf_;
    }
    inline virtual void set_data_buf(Buffer* buffer){
        data_buf_ = buffer;
        return;
    }
    inline virtual Buffer* get_request_buf(){
        return buf_[0];
    }
    inline virtual Buffer* get_inline_buf(){
        return buf_[1];
    }
    
    virtual void on_send_done(ibv_wc &cqe) override;
    virtual void on_send_cancelled(bool err, int eno=0) override;
    virtual void on_recv_done(msg::RdmaConnection *conn, ibv_wc &cqe) override;
    virtual void on_recv_cancelled(bool err, int eno=0) override;
    void run();

    friend class ReadCmdService;
    friend class WriteCmdService;

};//class RdmaWorkRequest

//--------------------------RdmaWorkRequestPool-------------------------------------------------//
class RdmaWorkRequestPool{
    msg::MsgContext *msg_context_;
    Msger *msger_;
    std::deque<RdmaWorkRequest *> reqs_free_;
    Mutex mutex_;
    int expand_lockfree(int n);
    int purge_lockfree(int n);
public:
    explicit RdmaWorkRequestPool(msg::MsgContext *c, Msger *m);
    ~RdmaWorkRequestPool();

    int alloc_reqs(int n, std::vector<RdmaWorkRequest *> &reqs);
    RdmaWorkRequest* alloc_req();
    void free_req(RdmaWorkRequest *req);
    int purge(int n);
};//class RdmaWorkRequestPool

//--------------------------Msger在msg v2中几乎没什么用，回调已放到RdmaWorkRequest中-----------------------------------------------------//

class Msger : public msg::MsgerCallback{
    msg::MsgContext *msg_context_;
    RdmaWorkRequestPool pool_;
    bool is_server_;
    CmdClientStub* client_stub_;
public:
    explicit Msger(msg::MsgContext *c, CmdClientStub* client_stub, bool s) 
    : msg_context_(c), pool_(c, this), is_server_(s) {
        if(!is_server_ && client_stub != nullptr){ //客户端
            client_stub_ = client_stub;
        }
    };

    inline CmdClientStub* get_client_stub() const { return client_stub_; }
    virtual int get_recv_wrs(int n, std::vector<msg::RdmaRecvWr *> &wrs) override;
    virtual void on_rdma_env_ready() override;
    
    RdmaWorkRequestPool &get_req_pool() { return pool_; }
    bool is_server() { return is_server_; }
};//class Msger
} //namespace flame

#endif //FLAME_LIBFLAME_LIBCHUNK_MSG_HANDLE_H