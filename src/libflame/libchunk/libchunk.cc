/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 14:57:01
 * @LastEditors: lwg
 * @LastEditTime: 2019-10-12 15:11:02
 */
#include "libflame/libchunk/libchunk.h"

#include "include/csdc.h"
#include "log_libchunk.h"
#include "common/context.h"
#include "util/spdk_common.h"
#include "include/buffer.h"
#include "msg/rdma/Infiniband.h"
#include "memzone/mz_types.h"
#include "memzone/rdma/RdmaMem.h"
#include "util/ip_op.h"

#define MAX_INFLIGHT_REQ (1 << 16 - 1)
namespace flame {

//-------------------------------------MemoryAreaImpl->MemoryArea-------------------------------------------------------//
MemoryAreaImpl::MemoryAreaImpl(uint64_t addr, uint32_t len, uint32_t key, bool is_dma)
            : MemoryArea(), is_dma_(is_dma){
    ma_.addr = addr;
    ma_.len = len;
    ma_.key = key; 
}

bool MemoryAreaImpl::is_dma() const {
    return is_dma_;
}
void* MemoryAreaImpl::get_addr() const {
    return (void *)ma_.addr;
}
uint64_t MemoryAreaImpl::get_addr_uint64() const {
    return ma_.addr;
}
uint32_t MemoryAreaImpl::get_len() const {
    return ma_.len;
}
uint32_t MemoryAreaImpl::get_key() const {
    return ma_.key;
}
cmd_ma_t MemoryAreaImpl::get() const {
    return ma_;
}   

/**
 * @name: CmdClientStubImpl
 * @describtions: CmdClientStubImpl构造函数，通过FlameContext*构造MsgContext*
 * @param   FlameContext*       flame_context 
 *          msg_module_cb       clear_done_cb       在消息模块销毁后调用的函数
 * @return: \
 */
std::atomic<uint64_t> ring(0);     //用于填充cqn
CmdClientStubImpl::CmdClientStubImpl(FlameContext *flame_context, msg::msg_module_cb clear_done_cb)
    :  CmdClientStub(){
    msg_context_ = new msg::MsgContext(flame_context); //* set msg_context_
    msg_context_->clear_done_cb = clear_done_cb;
    msg_context_->clear_done_arg1 = msg_context_;
    client_msger_ = new Msger(msg_context_, this, false);
    if(msg_context_->load_config()){
        assert(false);
    }
    int r;
    r = msg_context_->config->set_msg_worker_type("SPDK");
    assert(!r);
    msg_context_->init(client_msger_);//* set msg_client_recv_func
}

uint64_t CmdClientStubImpl::_get_io_addr(uint64_t ip, uint16_t port){
    uint64_t io_addr = ip << 16 | port;
    return io_addr;
}

/**
 * @name: set_session
 * @describtions: CmdClientStubImpl设置session_
 * @param   std::string     ip_addr     字符串形式的IP地址       
 *          int             port        端口号 
 * @return: 0表示成功
 */
int CmdClientStubImpl::set_session(std::string ip_addr, int port){
    uint64_t ip = string_to_ip(ip_addr);
    int64_t io_addr = _get_io_addr(ip, (uint16_t)port);
    if(session_.count(io_addr) == 1)
        return 0;
    /**此处的NodeAddr仅作为msger_id_t唯一标识的参数，并不是实际通信地址的填充**/
    msg::NodeAddr* addr = new msg::NodeAddr(msg_context_);
    addr->ip_from_string(ip_addr);
    addr->set_port(port);
    msg::msger_id_t msger_id = msg::msger_id_from_msg_node_addr(addr);
    addr->put();
    /*此处填充通信地址,与msger_id类似*/
    msg::NodeAddr* rdma_addr = new msg::NodeAddr(msg_context_);
    rdma_addr->set_ttype(NODE_ADDR_TTYPE_RDMA);
    rdma_addr->ip_from_string(ip_addr);
    rdma_addr->set_port(port);
    msg::Session* single_session = msg_context_->manager->get_session(msger_id);
    single_session->set_listen_addr(rdma_addr, msg::msg_ttype_t::RDMA);
    rdma_addr->put();
    session_[io_addr] = single_session; 
    return 0;
}

/**
 * @name: CmdeClientStub
 * @describtions: 创建访问CSD的客户端句柄
 * @param   std::string     ip_addr     字符串形式的IP地址       
 *          int             port        端口号
 * @return: std::shared_ptr<CmdClientStubImpl>
 */
std::shared_ptr<CmdClientStubImpl> CmdClientStubImpl::create_stub(msg::msg_module_cb clear_done_cb){
    FlameContext* flame_context = FlameContext::get_context();
    CmdClientStubImpl* cmd_client_stub = new CmdClientStubImpl(flame_context, clear_done_cb);
    return std::shared_ptr<CmdClientStubImpl>(cmd_client_stub);
} 

/**
 * @name: get_request
 * @describtions: 从client_msger_的pool中获得request给用户进行操作，返回的RdmaWorkRequest->command在RDMA内存上，可以直接操作
 * @param 
 * @return: RdmaWorkRequest*
 */
RdmaWorkRequest* CmdClientStubImpl::get_request(){
    RdmaWorkRequest* req = client_msger_->get_req_pool().alloc_req();
    return req;
}

void CmdClientStubImpl::_prepare_inline(RdmaWorkRequest& req, uint64_t bufaddr, uint32_t data_len){
    Buffer* inline_data_buf = req.get_inline_buf();
    char* inline_data  = (char*)inline_data_buf->addr();
    strncpy(inline_data, (char*)bufaddr, data_len);
    (req.get_ibv_send_wr())->num_sge = 2;
}

uint32_t CmdClientStubImpl::_get_command_queue_n(RdmaWorkRequest& req){
    return ((cmd_t *)req.command)->hdr.cqg << 16 | ((cmd_t *)req.command)->hdr.cqn;
}

/**
 * @name: submit 
 * @describtions: libflame端提交请求到CSD端 
 * @param   
 * @return: 
 */
int CmdClientStubImpl::submit(RdmaWorkRequest& req, uint64_t io_addr, cmd_cb_fn_t cb_fn, Buffer* buffer, void* cb_arg){
    ((cmd_t *)req.command)->hdr.cqn = ((ring++)%MAX_INFLIGHT_REQ);
    msg::Connection* conn = session_[io_addr]->get_conn(msg::msg_ttype_t::RDMA);
    msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
    if(((cmd_t *)req.command)->hdr.cn.seq == CMD_CHK_IO_WRITE){
        ChunkWriteCmd* write_cmd = new ChunkWriteCmd((cmd_t *)req.command);
        if(write_cmd->get_inline_data_len() > 0){
            uint32_t data_len = write_cmd->get_ma_len();
            _prepare_inline(req, (uint64_t)buffer->addr(), data_len);
        }
    }
    req.set_data_buf(buffer);
    struct MsgCallBack msg_cb;
    msg_cb.cb_fn    = cb_fn;
    msg_cb.buffer   = req.get_data_buf(); 
    msg_cb.cb_arg   = cb_arg;
    uint32_t cq = _get_command_queue_n(req);
    FlameContext* flame_context = FlameContext::get_context();
    flame_context->log()->ldebug("submit command queue num : 0x%x", cq);
    msg_cb_map_.insert(std::map<uint32_t, MsgCallBack>::value_type (cq, msg_cb));
    rdma_conn->post_send(&req);
    return 0;
}

//-------------------------------------CmdServerStubImpl->CmdServerStub-------------------------------------------------//
CmdServerStubImpl::CmdServerStubImpl(FlameContext* flame_context){
    msg_context_ = new msg::MsgContext(flame_context); //* set msg_context_
    server_msger_ = new Msger(msg_context_, nullptr, true);
    assert(!msg_context_->load_config());
    int r;
    r = msg_context_->config->set_msg_worker_type("SPDK");
    assert(!r);
    msg_context_->init(server_msger_);//* set msg_server_recv_func
}

} // namespace flame
