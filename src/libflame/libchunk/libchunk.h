/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:02:43
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-10 11:39:40
 */
#ifndef FLAME_LIBFLAME_LIBCHUNK_LIBCHUNK_H
#define FLAME_LIBFLAME_LIBCHUNK_LIBCHUNK_H

#include "include/cmd.h"

#include "msg/msg_core.h"
#include "libflame/libchunk/msg_handle.h"
#include "msg/msg_context.h"

namespace flame {

class MemoryAreaImpl : public MemoryArea{
public:
    MemoryAreaImpl(uint64_t addr, uint32_t len, uint32_t key, bool is_dma);
    
    ~MemoryAreaImpl(){}

    virtual bool is_dma() const override;
    virtual void *get_addr() const override;
    virtual uint64_t get_addr_uint64() const override;
    virtual uint32_t get_len() const override;
    virtual uint32_t get_key() const override;
    virtual cmd_ma_t get() const override;

private:
    cmd_ma_t ma_;  //* including addr, len, key
    bool is_dma_;
};

class CmdClientStubImpl : public CmdClientStub{
public:
    static std::shared_ptr<CmdClientStubImpl> create_stub(msg::msg_module_cb clear_done_cb);
    
    CmdClientStubImpl(FlameContext* flame_context, msg::msg_module_cb clear_done_cb);

    ~CmdClientStubImpl() {
        msg_context_->fin();
    } 
    static int ring;     //用于填充cqn
    
    RdmaWorkRequest* get_request();
    inline virtual std::map<uint32_t, MsgCallBack>& get_cb_map() override {return msg_cb_map_;}
    virtual int submit(RdmaWorkRequest& req, uint64_t io_addr, cmd_cb_fn_t cb_fn, Buffer* buffer, void* cb_arg) override;
    int set_session(std::string ip_addr, int port);
private:
    msg::MsgContext* msg_context_;
    Msger* client_msger_;
    std::map<uint64_t , msg::Session*> session_;

    uint64_t _get_io_addr(uint64_t ip, uint16_t port);
    void _prepare_inline(RdmaWorkRequest& req, uint64_t bufaddr, uint32_t data_len);
    uint32_t _get_cq(RdmaWorkRequest& req);
}; // class CmdClientStubImpl


class CmdServerStubImpl : public CmdServerStub{
public:
    CmdServerStubImpl(FlameContext* flame_context);
    virtual ~CmdServerStubImpl() {
        msg_context_->fin();
    }
private:
    msg::MsgContext* msg_context_;
    Msger* server_msger_;
}; // class CmdServerStubImpl
} // namespace flame

#endif //FLAME_LIBFLAME_LIBCHUNK_LIBCHUNK_H