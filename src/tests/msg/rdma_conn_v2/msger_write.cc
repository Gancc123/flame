/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 16:56:39
 */
#include "msger_write.h"

#include "util/clog.h"

namespace flame{
namespace msg{

RwRequest* RwRequest::create(MsgContext *c, RwMsger *m){
    auto req = new RwRequest(c, m);
    if(!req){
        return nullptr;
    }
    auto buffer = Stack::get_rdma_stack()->get_rdma_allocator()->allocate_ptr(4096);
    if(!buffer){
        delete req;
        return nullptr;
    }
    req->buf = buffer;
    req->sge.addr = (uint64_t)buffer->addr();
    req->sge.length = 64;
    req->sge.lkey = buffer->lkey();
    ibv_send_wr &swr = req->send_wr;
    memset(&swr, 0, sizeof(swr));
    //注意此处的写法
    swr.wr_id = reinterpret_cast<uint64_t>((RdmaSendWr *)req);
    swr.opcode = IBV_WR_SEND;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = 1;
    swr.sg_list = &req->sge;

    ibv_recv_wr &rwr = req->recv_wr;
    memset(&rwr, 0, sizeof(rwr));
    //注意此处的写法
    rwr.wr_id = reinterpret_cast<uint64_t>((RdmaRecvWr *)req);
    rwr.num_sge = 1;
    rwr.sg_list = &req->sge;

    req->data = (memory_info *)req->buf->buffer();

    return req;
}

RwRequest::~RwRequest(){
    if(buf){
        Stack::get_rdma_stack()->get_rdma_allocator()->get_allocator_ctx()->free(buf);
        buf = nullptr;
    }
}

void RwRequest::on_send_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void RwRequest::on_recv_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void RwRequest::on_send_done(ibv_wc &cqe){
    ML(mct, debug, "");
    if(cqe.status == IBV_WC_SUCCESS){
        status = SEND_DONE;
    }else{
        status = ERROR;
    }
    run();
}

void RwRequest::on_recv_done(RdmaConnection *conn, ibv_wc &cqe){
    ML(mct, debug, "");
    this->conn = conn;
    if(cqe.status == IBV_WC_SUCCESS){
        status = RECV_DONE;
    }else{
        status = ERROR;
    }
    run();
}

void RwRequest::run(){
    bool next_ready;
    if(msger->is_server()){
        do{
            next_ready = false;
            switch(status){
            case RECV_DONE:{
                ML(mct, info, "Recv from client data->addr: {:x}", data->addr);
                flame::memory::ib::RdmaBufferAllocator* allocator = Stack::get_rdma_stack()->get_rdma_allocator();
                flame::memory::ib::RdmaBuffer* lbuf = allocator->alloc(4096); //获取一片本地的内存

                buf = lbuf;
                sge.addr = lbuf->addr();
                sge.length = 4096;
                sge.lkey = lbuf->lkey();

                send_wr.sg_list = &sge;
                send_wr.wr.rdma.remote_addr = data->addr;
                send_wr.wr.rdma.rkey = data->rkey;

                send_wr.opcode = IBV_WR_RDMA_WRITE;

                status = EXEC_DONE;
                next_ready = true;
                break;
            }
            case EXEC_DONE:
                assert(conn);
                conn->post_send(this);
                break;
            case SEND_DONE:
                ML(mct, info, "send done!!!!!!!!!!!");
                status = DESTROY;
                next_ready = true;
                break;
            case DESTROY:
            case ERROR:
                msger->get_req_pool().free_req(this);
                status = FREE;
                break;
            };
        }while(next_ready);
    }else{
        do{
            next_ready = false;
            switch(status){
            case RECV_DONE:
                ML(mct, info, "Recv from server");
                // if(data->count >= 200){
                //     clog("client iter done.");
                //     status = DESTROY;
                //     next_ready = true;
                //     break;
                // }
                assert(conn);
                conn->post_send(this);
                break;
            case SEND_DONE:
                status = DESTROY;
                next_ready = true;
                break;
            case DESTROY:
            case ERROR:
                msger->get_req_pool().free_req(this);
                status = FREE;
                break;
            };
        }while(next_ready);
    }
    
}



RwRequestPool::RwRequestPool(MsgContext *c, RwMsger *m)
:mct(c), msger(m), mutex_(MUTEX_TYPE_ADAPTIVE_NP){

}

RwRequestPool::~RwRequestPool(){
}

int RwRequestPool::expand_lockfree(int n){
    int i;
    for(i = 0;i < n;++i){
        auto req = RwRequest::create(mct, msger);
        if(!req) break;
        reqs_free.push_back(req);
    }
    return i;
}

int RwRequestPool::purge_lockfree(int n){
    int cnt = 0;
    while(!reqs_free.empty() && cnt != n){
        auto req = reqs_free.back();
        delete req;
        reqs_free.pop_back();

        ++cnt;
    }
    return cnt;
}

int RwRequestPool::alloc_reqs(int n, std::vector<RwRequest *> &reqs){
    MutexLocker l(mutex_);
    if(n > reqs_free.size()){
        expand_lockfree(n - reqs_free.size());
    }
    if(n > reqs_free.size()){
        n = reqs_free.size();
    }
    reqs.insert(reqs.begin(), reqs_free.end()-n, reqs_free.end());
    reqs_free.erase(reqs_free.end()-n, reqs_free.end());
    return n;
}

RwRequest *RwRequestPool::alloc_req(){
    MutexLocker l(mutex_);
    if(reqs_free.empty()){
        if(expand_lockfree(1) <= 0){
            return nullptr;
        }
    }
    auto req = reqs_free.back();
    reqs_free.pop_back();
    return req;
}

void RwRequestPool::free_req(RwRequest *req){
    MutexLocker l(mutex_);
    reqs_free.push_back(req);
}

int RwRequestPool::expand(int n){
    MutexLocker l(mutex_);
    return expand_lockfree(n);
}

int RwRequestPool::purge(int n){
    MutexLocker l(mutex_);
    return purge_lockfree(n);
}

int RwMsger::get_recv_wrs(int n, std::vector<RdmaRecvWr *> &wrs){
    std::vector<RwRequest *> reqs;
    int rn = pool.alloc_reqs(n, reqs);
    wrs.reserve(reqs.size());
    for(auto r : reqs){
        wrs.push_back((RdmaRecvWr *)r);
    }
    return rn;
}

void RwMsger::on_conn_declared(Connection *conn, Session *s){
    ML(mct, info, "Session: {}", s->to_string());
}

}// namespace msg
}// namespace flame