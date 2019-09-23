/*
 * @Descripttion: 
 * @version: 
 * @Author: liweiguang
 * @Date: 2019-05-16 14:56:17
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-21 10:09:10
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
int RdmaWorkRequest::allocate_send_buffer(RdmaWorkRequest* req){
    if(!req){
        return 1;
    }
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator(); 
    
    Buffer* buffer = allocator->allocate_ptr(4096);
    req->buf_[0] = buffer;
    req->sge_[0].addr = (uint64_t)buffer->addr();
    req->sge_[0].length = 64;
    req->sge_[0].lkey = buffer->lkey();
    Buffer* buffer2 = allocator->allocate_ptr(4096);
    req->buf_[1] = buffer2;
    req->sge_[1].addr = (uint64_t)buffer2->addr();
    req->sge_[1].length = 4096;
    req->sge_[1].lkey = buffer2->lkey();
    return 0;
}

int RdmaWorkRequest::prepare_send_recv(RdmaWorkRequest* req){
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
    return 0;
}

int RdmaWorkRequest::set_command(RdmaWorkRequest* req, void* addr){
    req->command = addr;
    return 0;
}

uint32_t RdmaWorkRequest::_get_cqgcqn(){
    //TODO 判断command是否有值
    return ((cmd_res_t*)command)->hdr.cqg << 16 | ((cmd_res_t*)command)->hdr.cqn;
}

bool RdmaWorkRequest::_judge_seq_type(int type){
    if(((cmd_res_t*)command)->hdr.cn.seq == type) return true;
    else return false;
}
/**
 * @name: create_request
 * @describtions: 创建send/recv融为一体的RDMA work request
 * @param   msg::MsgContext*        msg_context         Msg上下文
 *          Msger*                  msger               现在基本功能只是用来post recv_request以及区分服务器/客户端
 * @return: RdmaWorkRequest*
 */
RdmaWorkRequest* RdmaWorkRequest::create_request(msg::MsgContext* msg_context, Msger* msger){
    RdmaWorkRequest* req = new RdmaWorkRequest(msg_context, msger);
    if(!req)    return nullptr;
    int rc = allocate_send_buffer(req);
    if(rc != 0) return nullptr;
    rc = prepare_send_recv(req);
    if(rc != 0) return nullptr;
    rc = set_command(req, req->buf_[0]->addr());
    if(rc != 0) return nullptr;
    return req;
}

RdmaWorkRequest::~RdmaWorkRequest(){  
}

//**以下四个函数都是状态机的变化，最终会调用run()来执行实际的操作
void RdmaWorkRequest::on_send_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

void RdmaWorkRequest::on_recv_cancelled(bool err, int eno){
    status = DESTROY;
    run();
}

/* on_send_done cqe(completion queue element)判断发送的类型类型 */
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

void RdmaWorkRequest::on_recv_done(msg::RdmaConnection *conn, ibv_wc &cqe){
    this->conn = conn;
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
 */
void RdmaWorkRequest::run(){
    FlameContext* fct = FlameContext::get_context();
    bool next_ready;
    if(msger_->is_server()){
        do{
            next_ready = false;
            switch(status){
                case RECV_DONE:{
                    CmdServiceMapper* cmd_service_mapper = CmdServiceMapper::get_cmd_service_mapper(); 
                    fct->log()->ldebug("RdmaworkRequest::run: %u",spdk_env_get_current_core());
                    CmdService* service = cmd_service_mapper->get_service(((cmd_t *)command)->hdr.cn.cls, ((cmd_t *)command)->hdr.cn.seq);
                    this->service_ = service;
                    service_->call(this);                    
                    break;
                } 
                case EXEC_DONE:
                case READ_DONE:         
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
                CmdClientStub* cmd_clientstub = msger_->get_client_stub();
                std::map<uint32_t, MsgCallBack>& cb_map = cmd_clientstub->get_cb_map();
                uint32_t key = _get_cqgcqn();
                MsgCallBack msg_cb = cb_map[key];
                if(_judge_seq_type(CMD_CHK_IO_READ) && msg_cb.cb_fn != nullptr){  //读操作
                    ChunkReadRes* res = new ChunkReadRes((cmd_res_t*)command);
                    if(res->get_inline_len() > 0 && res->get_inline_len() <= MAX_INLINE_SIZE){
                        fct->log()->ldebug("inline read !!");
                        msg_cb.cb_fn(*(Response *)res, msg_cb.buffer, msg_cb.cb_arg);
                    }
                    else {
                        fct->log()->ldebug("uninline read !!");
                        msg_cb.cb_fn(*(Response *)res, msg_cb.buffer, msg_cb.cb_arg);
                    }       
                }
                else if(_judge_seq_type(CMD_CHK_IO_WRITE) && msg_cb.cb_fn != nullptr){           //写操作
                    CommonRes* res = new CommonRes((cmd_res_t*)command);
                    msg_cb.cb_fn(*(Response *)res, msg_cb.buffer, msg_cb.cb_arg);
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
 * @name: on_rdma_env_ready
 * @describtions: RDMA环境已设置好的回调，其中RDMA环境如protect domain等
 * @param  
 * @return: 
 */
void Msger::on_rdma_env_ready(){
    FlameContext* fct = FlameContext::get_context();
    fct->log()->ldebug("RDMA Env Ready!");
    /*第一次需要传入参数构建全局的RdmaAllocator*/
    MemoryConfig *mem_cfg = MemoryConfig::load_config(fct);
    assert(mem_cfg);
    flame::msg::ib::ProtectionDomain *pd = flame::msg::Stack::get_rdma_stack()->get_rdma_manager()->get_ib().get_pd();
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator(fct, pd, mem_cfg);
};
} //namespace flame