#ifndef FLAME_LIBFLAME_LIBCHUNK_LIBCHUNK_H
#define FLAME_LIBFLAME_LIBCHUNK_LIBCHUNK_H

#include "include/cmd.h"

#include "msg/msg_core.h"
#include "libflame/libchunk/msg_handle.h"
#include "msg/msg_context.h"


namespace flame {

//------------------------------------MemoryAreaImpl->MemoryArea--------------------------------------------------------//
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


//-------------------------------------CmdClientStubImpl->CmdClientStub-------------------------------------------------//

class CmdClientStubImpl : public CmdClientStub{
public:
    static std::shared_ptr<CmdClientStubImpl> create_stub(std::string ip_addr, int port, msg::msg_module_cb clear_done_cb);
    
    RdmaWorkRequest* get_request();

    static int ring;     //用于填充cqn

    inline virtual std::map<uint32_t, MsgCallBack>& get_cb_map() override {return msg_cb_map_;}

    virtual int submit(RdmaWorkRequest& req, cmd_cb_fn_t cb_fn, void* cb_arg) override;

    CmdClientStubImpl(FlameContext* flame_context, msg::msg_module_cb clear_done_cb);

    ~CmdClientStubImpl() {
        // client_msger_->clear_rw_buffers();
        msg_context_->fin();
    }

private:
    int _set_session(std::string ip_addr, int port);

    msg::MsgContext* msg_context_;
    Msger* client_msger_;
    msg::Session* session_;

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