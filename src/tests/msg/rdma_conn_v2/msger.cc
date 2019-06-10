#include "msger.h"

#include "util/clog.h"

namespace flame{
namespace msg{

Request* Request::create(MsgContext *c, Msger *m){
    auto req = new Request(c, m);
    if(!req){
        return nullptr;
    }
    auto allocator = Stack::get_rdma_stack()->get_rdma_allocator();
    auto buffer = allocator->alloc(4096);
    if(!buffer){
        delete req;
        return nullptr;
    }
    auto data_buffer = allocator->alloc(4096);
    if(!data_buffer){
        delete req;
        allocator->free(buffer);
        return nullptr;
    }
    req->buf = buffer;
    req->data_buffer = data_buffer;
    req->sge.addr = buffer->addr();
    req->sge.length = sizeof(test_data_t) > 64 ? sizeof(test_data_t) : 64;
    req->sge.lkey = buffer->lkey();

    req->data_sge.addr = data_buffer->addr();
    req->data_sge.length = data_buffer->size();
    req->data_sge.lkey = data_buffer->lkey();

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

    req->data = (test_data_t *)req->buf->buffer();
    req->data->ignore = 0;
    req->data->raddr = data_buffer->addr();
    req->data->rkey = data_buffer->rkey();
    req->data->length = data_buffer->size();
    req->data->is_read = 0;

    return req;
}

Request::~Request(){
    auto allocator = Stack::get_rdma_stack()->get_rdma_allocator();
    assert(allocator);
    if(buf){
        allocator->free(buf);
        buf = nullptr;
    }
    if(data_buffer){
        allocator->free(data_buffer);
        data_buffer = nullptr;
    }
}

void Request::on_send_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void Request::on_recv_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void Request::on_send_done(ibv_wc &cqe){
    ML(mct, debug, "");
    if(cqe.status == IBV_WC_SUCCESS){
        switch(cqe.opcode){
        case IBV_WC_SEND:
            status = SEND_DONE;
            break;
        case IBV_WC_RDMA_WRITE:
            status = WRITE_DONE;
            break;
        case IBV_WC_RDMA_READ:
            status = READ_DONE;
            break;
        default:
            status = ERROR;
            break;
        }
    }else{
        status = ERROR;
    }
    run();
}

void Request::on_recv_done(RdmaConnection *conn, ibv_wc &cqe){
    ML(mct, debug, "");
    this->conn = conn;
    if(cqe.status == IBV_WC_SUCCESS){
        status = RECV_DONE;
    }else{
        status = ERROR;
    }
    run();
}

void Request::run(){
    bool next_ready;
    if(msger->is_server()){
        do{
            next_ready = false;
            switch(status){
            case RECV_DONE:
                ML(mct, info, "Recv from client: {}", data->count);
                ML(mct, info, "{} raddr: {:x}, rkey: {:x}, "
                                "laddr: {:x}, lkey:{:x}, len:{}",
                                data->is_read?"READ":"WRITE",
                                data->raddr, data->rkey,
                                data_sge.addr,
                                data_sge.lkey,
                                data_sge.length);

                send_wr.sg_list = &data_sge;
                send_wr.opcode = data->is_read ? 
                                    IBV_WR_RDMA_READ :
                                    IBV_WR_RDMA_WRITE;
                send_wr.wr.rdma.remote_addr = data->raddr;
                send_wr.wr.rdma.rkey = data->rkey;

                conn->post_send(this);
                break;
            case WRITE_DONE:
            case READ_DONE:
                ML(mct, info, "{} done.", data->is_read?"read":"write");
                ++data->count;
                status = EXEC_DONE;
                next_ready = true;
                break;
            case EXEC_DONE:
                assert(conn);
                send_wr.sg_list = &sge;
                send_wr.opcode = IBV_WR_SEND;
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
    }else{
        do{
            next_ready = false;
            switch(status){
            case RECV_DONE:
                ML(mct, info, "Recv from server: {}", data->count);
                if(data->count >= 200){
                    clog("client iter done.");
                    status = DESTROY;
                    next_ready = true;
                    break;
                }
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



RequestPool::RequestPool(MsgContext *c, Msger *m)
:mct(c), msger(m), mutex_(MUTEX_TYPE_ADAPTIVE_NP){

}

RequestPool::~RequestPool(){
}

int RequestPool::expand_lockfree(int n){
    int i;
    for(i = 0;i < n;++i){
        auto req = Request::create(mct, msger);
        if(!req) break;
        reqs_free.push_back(req);
    }
    return i;
}

int RequestPool::purge_lockfree(int n){
    int cnt = 0;
    while(!reqs_free.empty() && cnt != n){
        auto req = reqs_free.back();
        delete req;
        reqs_free.pop_back();

        ++cnt;
    }
    return cnt;
}

int RequestPool::alloc_reqs(int n, std::vector<Request *> &reqs){
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

Request *RequestPool::alloc_req(){
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

void RequestPool::free_req(Request *req){
    MutexLocker l(mutex_);
    reqs_free.push_back(req);
}

int RequestPool::expand(int n){
    MutexLocker l(mutex_);
    return expand_lockfree(n);
}

int RequestPool::purge(int n){
    MutexLocker l(mutex_);
    return purge_lockfree(n);
}

int Msger::get_recv_wrs(int n, std::vector<RdmaRecvWr *> &wrs){
    std::vector<Request *> reqs;
    int rn = pool.alloc_reqs(n, reqs);
    wrs.reserve(reqs.size());
    for(auto r : reqs){
        wrs.push_back((RdmaRecvWr *)r);
    }
    return rn;
}

void Msger::on_conn_declared(Connection *conn, Session *s){
    ML(mct, info, "Session: {}", s->to_string());
}

}// namespace msg
}// namespace flame