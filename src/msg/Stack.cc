#include "Stack.h"
#include "common/context.h"
#include "msg/msg_context.h"
#include "msg/socket/TcpStack.h"
#include "msg_def.h"

#ifdef HAVE_RDMA
    #include "msg/rdma/RdmaStack.h"
#endif

namespace flame{

MsgContext *Stack::mct = nullptr;

TcpStack* Stack::get_tcp_stack(){
    static TcpStack instance(Stack::mct);
    return &instance;
}

int Stack::init_all_stack(MsgContext *c){
    if(c == nullptr) return -1;
    Stack::mct = c;
    int r = 0;
    r = Stack::get_tcp_stack()->init();
#ifdef HAVE_RDMA
    if(r) return r;
    if(Stack::mct->config->rdma_enable){
        r = Stack::get_rdma_stack()->init();
    }
#endif
    return r;
}

int Stack::clear_all_before_stop(){
    int r = 0;
#ifdef HAVE_RDMA
    if(Stack::mct->config->rdma_enable){
        r = Stack::get_rdma_stack()->clear_before_stop();
    }
    if(r) return r;
#endif
    r = Stack::get_tcp_stack()->clear_before_stop();
    return r;
}

int Stack::fin_all_stack(){
    int r = 0;
#ifdef HAVE_RDMA
    if(Stack::mct->config->rdma_enable){
        r = Stack::get_rdma_stack()->fin();
    }
    if(r) return r;
#endif
    r = Stack::get_tcp_stack()->fin();
    return r;
}

#ifdef HAVE_RDMA
RdmaStack* Stack::get_rdma_stack(){
    if(!Stack::mct->config->rdma_enable){
        ML(Stack::mct, error, "RDMA disabled!");
        return nullptr;
    }
    static RdmaStack instance(Stack::mct);
    return &instance;
}
#endif

}