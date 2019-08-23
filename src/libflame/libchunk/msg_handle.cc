/*
 * @Descripttion: 
 * @version: 
 * @Author: liweiguang
 * @Date: 2019-05-16 14:56:17
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-20 11:20:38
 */
#include "libflame/libchunk/msg_handle.h"

#include "include/csdc.h"
#include "util/clog.h"
#include "include/retcode.h"
#include "libflame/libchunk/log_libchunk.h"
#include "include/cmd.h"
#include "libflame/libchunk/chunk_cmd_service.h"
#include "util/spdk_common.h"

namespace flame {

//--------------------------RdmaWorkRequest------------------------------------------------------
/**
 * @name: create_request
 * @describtions: 创建send/recv融为一体的RDMA work request
 * @param   msg::MsgContext*        msg_context         Msg上下文
 *          Msger*                  msger               现在基本功能只是用来post recv_request以及区分服务器/客户端
 * @return: RdmaWorkRequest*
 */
RdmaWorkRequest* RdmaWorkRequest::create_request(msg::MsgContext* msg_context, Msger* msger){
    RdmaWorkRequest* req = new RdmaWorkRequest(msg_context, msger);
    if(!req){
        return nullptr;
    }
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator(); 
    Buffer buffer = allocator->allocate(4096);//4KB这里的buffer会自己释放吗？
    Buffer data_buffer = allocator->allocate(4096);//4KB这里的buffer会自己释放吗？
    req->buf_ = buffer;
    req->sge_[0].addr = (uint64_t)buffer.addr();
    req->sge_[0].length = 64;
    req->sge_[0].lkey = buffer.lkey();
    req->data_buf_ = data_buffer;
    req->sge_[1].addr = (uint64_t)data_buffer.addr();
    req->sge_[1].length = 4096;
    req->sge_[1].lkey = data_buffer.lkey();

    ibv_send_wr &swr = req->send_wr_;
    memset(&swr, 0, sizeof(swr));
    swr.wr_id = reinterpret_cast<uint64_t>((RdmaSendWr *)req);
    swr.opcode = IBV_WR_SEND;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = 1;
    swr.sg_list = req->sge_;

    ibv_recv_wr &rwr = req->recv_wr_;
    memset(&rwr, 0, sizeof(rwr));
    rwr.wr_id = reinterpret_cast<uint64_t>((RdmaRecvWr *)req);
    rwr.num_sge = 2;
    rwr.sg_list = req->sge_;

    req->command = req->buf_.addr();
    return req;
}


RdmaWorkRequest::~RdmaWorkRequest(){
    
}

//**以下四个函数都是状态机的变化，最终会调用run()来执行实际的操作
/**
 * @name: on_send_cancelled
 * @describtions: 
 * @param 
 * @return: 
 */
void RdmaWorkRequest::on_send_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void RdmaWorkRequest::on_recv_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

/**
 * @name: on_send_done
 * @describtions: RDMA发送成功后的回调
 * @param   ibv_wc&             cqe         completion queue element判断是否成功
 * @return: 
 */
void RdmaWorkRequest::on_send_done(ibv_wc &cqe){
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

/**
 * @name: on_recv_done
 * @describtions: 接收到WorkRequest的回调
 * @param   RdmaConnection*     conn        对端的连接
 *          ibv_wc&             cqe         完成元素，接受是否成功            
 * @return: 
 */
void RdmaWorkRequest::on_recv_done(msg::RdmaConnection *conn, ibv_wc &cqe){
    this->conn = conn;
    ML(msg_context_, info, "I recv !!");
    if(cqe.status == IBV_WC_SUCCESS){
        status = RECV_DONE;
    }else{
        status = ERROR;
    }
    run();
}

/**
 * @name: run
 * @describtions: 状态机变化的真正执行函数，包括五种状态
 * @param 
 * @return: 
 */
void RdmaWorkRequest::run(){
    bool next_ready;
    if(msger_->is_server()){
        do{
            next_ready = false;
            switch(status){
                case RECV_DONE:{
                    CmdServiceMapper* cmd_service_mapper = CmdServiceMapper::get_cmd_service_mapper(); 
                    FlameContext* fct = FlameContext::get_context();
                    fct->log()->ldebug("RdmaworkRequest::run: %u",spdk_env_get_current_core());
                    CmdService* service = cmd_service_mapper->get_service(((cmd_t *)command)->hdr.cn.cls, ((cmd_t *)command)->hdr.cn.seq);
                    this->service_ = service;
                    service_->call(this);                    
                    break;
                } 
                case EXEC_DONE:
                case READ_DONE:            //**如果是read/write，SEND_DONE是否代表read/write已经成功????????????????????????? **//
                case WRITE_DONE:
                    service_->call(this);
                    break;   
                case SEND_DONE:             //send response done                        
                    next_ready = true;
                    status = DESTROY;
                    break;
                case DESTROY:
                case ERROR:
                    msger_->get_req_pool().free_req(this);
                    status = FREE;
                    break;
            };
        }while(next_ready);
    }else{
        do{
            next_ready = false;
            switch(status){
            case RECV_DONE:{
                std::map<uint32_t, MsgCallBack>& cb_map = msger_->get_client_stub()->get_cb_map();
                uint32_t key = ((cmd_res_t*)command)->hdr.cqg << 16 | ((cmd_res_t*)command)->hdr.cqn;
                MsgCallBack msg_cb = cb_map[key];
                if(((cmd_res_t*)command)->hdr.cn.seq == CMD_CHK_IO_READ && msg_cb.cb_fn != nullptr){  //读操作
                    ChunkReadRes* res = new ChunkReadRes((cmd_res_t*)command);
                    if(res->get_inline_len() > 0 && res->get_inline_len() <= MAX_INLINE_SIZE){
                        FlameContext* fct = FlameContext::get_context();
                        fct->log()->ldebug("inline read !!");
                        msg_cb.cb_fn(*(Response *)res, (uint64_t)data_buf_.addr(), msg_cb.cb_arg);
                    }
                    else {
                        FlameContext* fct = FlameContext::get_context();
                        fct->log()->ldebug("uninline read !!");
                        msg_cb.cb_fn(*(Response *)res, msg_cb.buf, msg_cb.cb_arg);
                    }
                        
                }
                else if(((cmd_res_t*)command)->hdr.cn.seq == CMD_CHK_IO_WRITE && msg_cb.cb_fn != nullptr){           //写操作
                    CommonRes* res = new CommonRes((cmd_res_t*)command);
                    msg_cb.cb_fn(*(Response *)res, msg_cb.buf, msg_cb.cb_arg);
                }
                cb_map.erase(key);
                status = DESTROY;
                next_ready = true;
                break;
            }
            case SEND_DONE:
                status = DESTROY;
                next_ready = true;
                break;
            case DESTROY:
            case ERROR:
                msger_->get_req_pool().free_req(this);
                status = FREE;
                break;
            };
        }while(next_ready);
    }
}

//-------------------------------------------RdmaWorkRequestPool------------------------------------//
RdmaWorkRequestPool::RdmaWorkRequestPool(msg::MsgContext *c, Msger *m)
    :msg_context_(c), msger_(m), mutex_(MUTEX_TYPE_ADAPTIVE_NP){

}

RdmaWorkRequestPool::~RdmaWorkRequestPool(){
}

/**
 * @name: expand_lockfree
 * @describtions: 创建RdmaWOrkRequest到reqs_free_待用
 * @param   int     n       创建的req数量
 * @return: 成功建立的req数量
 */
int RdmaWorkRequestPool::expand_lockfree(int n){
    int i;
    for(i = 0; i < n; i++){
        RdmaWorkRequest* req = RdmaWorkRequest::create_request(msg_context_, msger_);
        if(!req) break;
        reqs_free_.push_back(req);
    }
    return i;
}

/**
 * @name: purge_lockfree
 * @describtions: 释放申请的req，这些req需要在req_free_上
 * @param   int     n      指定释放的req数量，-1表示全释放
 * @return: 释放的req数量
 */
int RdmaWorkRequestPool::purge_lockfree(int n){
    int cnt = 0;
    while(!reqs_free_.empty() && cnt != n){
        RdmaWorkRequest* req = reqs_free_.back();
        delete req;
        reqs_free_.pop_back();
        cnt++;
    }
    return cnt;
}

/**
 * @name: alloc_reqs
 * @describtions: 从reqs_free_中取出一批reqs，如果不够则通过expand_lockfree进行创建
 * @param   vector<RdmaWorkRequest *>&      reqs        将取出的这批req放入传入的引用reqs中
 * @return: 采集的req数量
 */
int RdmaWorkRequestPool::alloc_reqs(int n, std::vector<RdmaWorkRequest *> &reqs){
    MutexLocker l(mutex_);
    if(n > reqs_free_.size()){
        expand_lockfree(n - reqs_free_.size());
    }
    if(n > reqs_free_.size()){
        n = reqs_free_.size();
    }
    reqs.insert(reqs.begin(), reqs_free_.end() - n, reqs_free_.end());
    reqs_free_.erase(reqs_free_.end() - n, reqs_free_.end());
    return n;
}

/**
 * @name: alloc_req
 * @describtions: 从reqs_free_中采集req，reqs_free_为空则create_request
 * @param 
 * @return: RdmaWorkRequest*    采集的一个RdmaWorkRequest指针 
 */
RdmaWorkRequest* RdmaWorkRequestPool::alloc_req(){
    MutexLocker l(mutex_);
    if(reqs_free_.empty()){
        if(expand_lockfree(1) <= 0){
            return nullptr;
        }
    }
    RdmaWorkRequest* req = reqs_free_.back();
    reqs_free_.pop_back();
    return req;
}

/**
 * @name: free_req
 * @describtions: 内部的释放是将req放入reqs_free_供循环利用
 * @param   RdmaWorkRequest*        req         放回reqs_free_的req指针
 * @return: 
 */
void RdmaWorkRequestPool::free_req(RdmaWorkRequest* req){
    MutexLocker l(mutex_);
    reqs_free_.push_back(req);
}
/**
 * @name: purge
 * @describtions: public函数，为用户提供释放req的接口
 * @param   int         n           释放的req个数，注意这里的purge就是真正的delete
 * @return: int 真正delete的req数量
 */
int RdmaWorkRequestPool::purge(int n){
    MutexLocker l(mutex_);
    return purge_lockfree(n);
}


//-------------------------------------------------Msger-------------------------------------------//
/**
 * @name: get_recv_wrs
 * @describtions: 提供给msg模块进行recv work request的填充
 * @param   int                     n           recv work request数量
 *          vector<RdmaRecvWr *>&   wrs         引用，获得n个RdmaRecvWr
 * @return: 获得的recv work request数量
 */
int Msger::get_recv_wrs(int n, std::vector<msg::RdmaRecvWr *> &wrs){
    std::vector<RdmaWorkRequest *> reqs;
    int rn = pool_.alloc_reqs(n, reqs);
    wrs.reserve(reqs.size());
    for(auto r : reqs){
        wrs.push_back((msg::RdmaRecvWr *)r);
    }
    return rn;
}

/**
 * @name: on_conn_declared
 * @describtions: 在将要建立连接时的回调
 * @param   Connection*     conn        
 *          Session*        s
 * @return: 
 */
void Msger::on_conn_declared(msg::Connection *conn, msg::Session *s){
    ML(msg_context_, info, "Session: {}", s->to_string());
}

/**
 * @name: on_conn_recv
 * @describtions: 接收到消息的回调
 * @param {type} 
 * @return: 
 */
void Msger::on_conn_recv(msg::Connection *conn, msg::Msg *msg){
    ML(msg_context_, info, "I recv something.");
};

/**
 * @name: on_rdma_env_ready
 * @describtions: RDMA环境已设置好的回调，其中RDMA环境如protect domain等
 * @param  
 * @return: 
 */
void Msger::on_rdma_env_ready(){
    ML(msg_context_, info, "I recv something.");
    /*第一次需要传入参数构建全局的RdmaAllocator*/
    FlameContext* fct = FlameContext::get_context();
    MemoryConfig *mem_cfg = MemoryConfig::load_config(fct);
    assert(mem_cfg);
    flame::msg::ib::ProtectionDomain *pd = flame::msg::Stack::get_rdma_stack()->get_manager()->get_ib().get_pd();
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator(fct, pd, mem_cfg);
};

} //namespace flame