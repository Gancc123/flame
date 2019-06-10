#include "libflame/libchunk/libchunk.h"

#include "include/csdc.h"
#include "log_libchunk.h"


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



//-------------------------------------CmdClientStubImpl->CmdClientStub-------------------------------------------------//
/**
 * @name: CmdClientStubImpl
 * @describtions: CmdClientStubImpl构造函数，通过FlameContext*构造MsgContext*
 * @param   FlameContext*       flame_context 
 * @return: \
 */

int CmdClientStubImpl::ring = 0;
CmdClientStubImpl::CmdClientStubImpl(FlameContext *flame_context)
    :  CmdClientStub(){
    msg_context_ = new msg::MsgContext(flame_context); //* set msg_context_
    client_msger_ = new Msger(msg_context_, this, false);
    if(msg_context_->load_config()){
        assert(false);
    }
    msg_context_->config->set_rdma_conn_version("2"); //**这一步很重要，转换成msg_v2
    msg_context_->init(client_msger_);//* set msg_client_recv_func
}

/**
 * @name: _set_session
 * @describtions: CmdClientStubImpl设置session_
 * @param   std::string     ip_addr     字符串形式的IP地址       
 *          int             port        端口号 
 * @return: 0表示成功
 */
int CmdClientStubImpl::_set_session(std::string ip_addr, int port){
    /**此处的NodeAddr仅作为msger_id_t唯一标识的参数，并不是实际通信地址的填充**/
    msg::NodeAddr* addr = new msg::NodeAddr(msg_context_);
    addr->ip_from_string(ip_addr);
    addr->set_port(port);
    msg::msger_id_t msger_id = msg::msger_id_from_msg_node_addr(addr);
    addr->put();
    session_ = msg_context_->manager->get_session(msger_id);
    /*此处填充通信地址*/
    msg::NodeAddr* rdma_addr = new msg::NodeAddr(msg_context_);
    rdma_addr->set_ttype(NODE_ADDR_TTYPE_RDMA);
    rdma_addr->ip_from_string(ip_addr);
    rdma_addr->set_port(port);
    session_->set_listen_addr(rdma_addr, msg::msg_ttype_t::RDMA);
    rdma_addr->put();
    return 0;
}

/**
 * @name: CmdeClientStub
 * @describtions: 创建访问CSD的客户端句柄
 * @param   std::string     ip_addr     字符串形式的IP地址       
 *          int             port        端口号
 * @return: std::shared_ptr<CmdClientStubImpl>
 */
std::shared_ptr<CmdClientStubImpl> CmdClientStubImpl::create_stub(std::string ip_addr, int port){
    FlameContext* flame_context = FlameContext::get_context();
    CmdClientStubImpl* cmd_client_stub = new CmdClientStubImpl(flame_context);
    int rc;
    rc = cmd_client_stub->_set_session(ip_addr, port);
    if(rc) return nullptr;
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


/**
 * @name: submit 
 * @describtions: libflame端提交请求到CSD端 
 * @param   Conmmand&           cmd         准备发送的命令
 *          cmd_cb_fn_t*        cb_fn       命令得到回复后的回调函数
 *          void*               cb_arg      回调函数的参数
 * @return: 
 */
int CmdClientStubImpl::submit(RdmaWorkRequest& req, cmd_cb_fn_t cb_fn, void* cb_arg){
    ((cmd_t *)req.command)->hdr.cqn = ((ring++)%256);
    msg::Connection* conn = session_->get_conn(msg::msg_ttype_t::RDMA);
    msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
    if(((cmd_t *)req.command)->hdr.cn.seq == CMD_CHK_IO_WRITE){
        ChunkWriteCmd* write_cmd = new ChunkWriteCmd((cmd_t *)req.command);
        FlameContext* fct = FlameContext::get_context();
        if(write_cmd->get_inline_data_len() > 0){
            (req.get_ibv_send_wr())->num_sge = 2;
        }
    }
    rdma_conn->post_send(&req);
    struct MsgCallBack msg_cb;
    msg_cb.cb_fn = cb_fn;
    msg_cb.cb_arg = cb_arg;
    uint32_t cq = ((cmd_t *)req.command)->hdr.cqg << 16 | ((cmd_t *)req.command)->hdr.cqn;
    msg_cb_map_.insert(std::map<uint32_t, MsgCallBack>::value_type (cq, msg_cb));
    
    return 0;
}


//-------------------------------------CmdServerStubImpl->CmdServerStub-------------------------------------------------//
CmdServerStubImpl::CmdServerStubImpl(FlameContext* flame_context){
    msg_context_ = new msg::MsgContext(flame_context); //* set msg_context_
    server_msger_ = new Msger(msg_context_, nullptr, true);
    assert(!msg_context_->load_config());
    msg_context_->config->set_rdma_conn_version("2"); //**这一步很重要，转换成msg_v2
    msg_context_->init(server_msger_);//* set msg_server_recv_func
}

} // namespace flame
