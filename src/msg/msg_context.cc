#include "msg_context.h"
#include "msg_common.h"
#include "msg/internal/msg_config.h"
#include "Msg.h"
#include "Message.h"
#include "Stack.h"
#include "MsgManager.h"
#include "msg/dispatcher/MsgDispatcher.h"

#ifdef HAVE_RDMA
    #include "msg/rdma/RdmaStack.h"
#endif

#ifdef HAVE_SPDK
    #include "spdk/env.h"
    #include "spdk/event.h"
    #include "spdk/thread.h"
#endif

#include <functional>
#include <tuple>
#include <string>

namespace flame{
namespace msg{

msg_log_level_t g_msg_log_level = msg_log_level_t::print;

int MsgContext::load_config(){
    if(config == nullptr){
        config = MsgConfig::load_config(fct);
    }
    if(config == nullptr){
        return -1;
    }
    return 0;
}

int MsgContext::init(MsgerCallback *msger_cb){
    int ret = 0;
    std::string transport;
    std::string address;
    int min_port, max_port, i;
    NodeAddr *addr = nullptr;
    MsgManager *msg_manager = nullptr;
    //fct can't be null.
    if(fct == nullptr) return -1;

    rand_init_helper::init();

    if(load_config()){
        return -1;
    }

    ML(this, info, "RdmaConnection Versoin: {}", config->rdma_conn_version);

#ifdef HAVE_SPDK
    if(config->msg_worker_type == msg_worker_type_t::SPDK){
        bind_core = spdk_env_get_current_core();
        ML(this, info, "Force to use direct poll "
                        "when msg worker type is SPDK.");
    }
#else //NO SPDK
    if(config->msg_worker_type == msg_worker_type_t::SPDK){
        ML(this, error, "Don't support msg worker type: SPDK.");
        ret = -1;
        goto err_config;
    }
#endif //HAVE_SPDK

    g_msg_log_level = config->msg_log_level;

    msg_manager = new MsgManager(this, config->msg_worker_num);
    msg_manager->set_msger_cb(msger_cb);

    this->manager = msg_manager;

    ML(this, trace, "#####  init all stack begin #####");
    if(Stack::init_all_stack(this)){
        ML(this, error, "init msg all stack failed!");
        ret = -1;
        goto err_manager;
    }
    ML(this, trace, "##### init all stack end #####");
    
    for(const auto &listen_port : config->node_listen_ports){
        std::tie(transport, address, min_port, max_port) = listen_port;
        if(!addr){
            addr = new NodeAddr(this);
        }
        addr->set_ttype(transport);
        addr->ip_from_string(address);
        auto ttype  = msg_ttype_from_str(transport);

        for(i = min_port; i <= max_port; ++i){
            addr->set_port(i);
            if(msg_manager->add_listen_port(addr, ttype)){
                ML(this, info, "listen: {}@{}/{}", transport, address, 
                                                        addr->get_port());
                addr->put();
                addr = nullptr;
                break;
            }
        }

        if(i > max_port){
            ML(this, warn, "add listen port failed : {}@{}/{}-{}", 
                                transport, address, min_port, max_port);
            if(addr){
                addr->put();
            }
            ret = -1;
            goto err_manager;
        }
    }
    if(addr){
        addr->put();
    }

    ML(this, trace, "##### msg manager start begin #####");
    msg_manager->start();
    ML(this, trace, "##### msg manager start end #####");

    this->state = msg_module_state_t::RUNNING;
    
    return 0;

err_manager:
    if(manager){
        delete manager;
        manager = nullptr;
    }
err_config:
    if(config){
        delete config;
        config = nullptr;
    }

    return ret;
}

static void fin_fn(void *arg1, void *arg2){
    MsgContext *mct = (MsgContext *)arg1;
    mct->fin();
}

int MsgContext::fin(){
    if(manager == nullptr) return 0;

#ifdef HAVE_SPDK
    if(config && config->msg_worker_type == msg_worker_type_t::SPDK){
        uint32_t cur_core = spdk_env_get_current_core();
        if(cur_core != bind_core){
            struct spdk_event *event = spdk_event_allocate(bind_core, 
                                                fin_fn,
                                                this, nullptr);
            assert(event);
            spdk_event_call(event);
            return 0;
        }
    }
#endif //HAVE_SPDK

    if(this->state == msg_module_state_t::INIT){
        return 0;
    }

    bool next_ready;
    auto msg_manager = manager;

    do{
        next_ready = false;
        if(this->state == msg_module_state_t::RUNNING){
            this->state = msg_module_state_t::CLEARING;
            ML(this, info, "msg module state: CLEARING");
            if(config->msg_worker_type == msg_worker_type_t::THREAD){
                MutexLocker l(thr_fin_mutex);
                thr_fin_ok = false;
            }
            msg_manager->clear_before_stop();

            ML(this, trace, "##### stack clear_all_before_stop begin #####");
            Stack::clear_all_before_stop();
            ML(this, trace, "##### stack clear_all_before_stop end #####");
        }else if(this->state == msg_module_state_t::CLEARING){
            if(config->msg_worker_type == msg_worker_type_t::THREAD){
                ML(this, info, "wait for clear done....");
                thr_fin_wait();
            }
            if(msg_manager->is_clear_done() && Stack::is_all_clear_done()){
                this->state = msg_module_state_t::CLEAR_DONE;
                ML(this, info, "msg module state: CLEAR_DONE");

                ML(this, trace, "##### msg manager stop begin #####");
                msg_manager->stop();
                ML(this, trace, "##### msg manager stop end #####");
                
                if(this->clear_done_cb){
                    ML(this, trace, "##### call clear_done_cb() #####");
                    this->clear_done_cb(this->clear_done_arg1,
                                        this->clear_done_arg2);
                }else{
                    next_ready = true;
                }
            }
        }else if(this->state == msg_module_state_t::CLEAR_DONE){
            this->state = msg_module_state_t::FIN;
            ML(this, info, "msg module state: FIN");

            ML(this, trace, "##### fin all stack begin #####");
            Stack::fin_all_stack();
            ML(this, trace, "##### fin all stack end #####");

            if(manager){
                delete manager;
                manager = nullptr;
            }
            
            if(config){
                delete config;
                config = nullptr;
            }
            
            next_ready = true;
        }else if(this->state == msg_module_state_t::FIN){
            if(this->fin_cb){
                ML(this, trace, "##### call fin_cb() #####");
                this->fin_cb(this->fin_arg1, this->fin_arg2);
            }
            break; //fin() is done.
        }
        if(!config || config->msg_worker_type != msg_worker_type_t::SPDK){
            next_ready = true;
        }
    }while(next_ready);

    return 0;
}

void MsgContext::clear_done_notify(){
    if(this->state != msg_module_state_t::CLEARING){ return; }
    if(config->msg_worker_type == msg_worker_type_t::THREAD){
        thr_fin_signal();
#ifdef HAVE_SPDK
    }else if(config->msg_worker_type == msg_worker_type_t::SPDK){
        struct spdk_event *event = spdk_event_allocate(bind_core, 
                                                fin_fn,
                                                this, nullptr);
        assert(event);
        spdk_event_call(event);
#endif //HAVE_SPDK
    }
}

void MsgContext::finally_fin(){
    if(this->state != msg_module_state_t::CLEAR_DONE){ return; }
    fin();
}

inline void MsgContext::thr_fin_wait(){
    MutexLocker l(thr_fin_mutex);
    while(!thr_fin_ok){
        thr_fin_cond.wait();
    }
}

inline void MsgContext::thr_fin_signal(){
    MutexLocker l(thr_fin_mutex);
    thr_fin_ok = true;
    thr_fin_cond.signal();
}

} //namespace msg
} //namespace flame