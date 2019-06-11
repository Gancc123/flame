#include "MsgManager.h"

#include "msg_common.h"
#include "msg_data.h"
#include "msg_types.h"
#include "Stack.h"
#include "socket/TcpStack.h"

#ifdef HAVE_RDMA
    #include "rdma/RdmaStack.h"
#endif

#ifdef HAVE_SPDK
    #include "spdk/SpdkMsgWorker.h"
#endif 

namespace flame{
namespace msg{

MsgManager::MsgManager(MsgContext *c, int worker_num)
: mct(c), is_running(false), m_mutex(MUTEX_TYPE_ADAPTIVE_NP){
    workers.reserve(worker_num);
    for(int i = 0;i < worker_num; ++i){
        MsgWorker *msg_worker = nullptr;
        if(mct->config->msg_worker_type == msg_worker_type_t::THREAD){
            msg_worker = new ThrMsgWorker(mct, i);
#ifdef HAVE_SPDK
        }else if(mct->config->msg_worker_type == msg_worker_type_t::SPDK){
            msg_worker = new SpdkMsgWorker(mct, i);
#endif //HAVE_SPDK
        }else{ //use ThrMsgWorker as default.
            msg_worker = new ThrMsgWorker(mct, i);
        }
        workers.push_back(msg_worker);
        if(!mct->config->msg_worker_cpu_map.empty()){
            int cpu_id = mct->config->msg_worker_cpu_map[i];
            workers[i]->set_affinity(cpu_id);
            ML(mct, info, "bind thr{} to cpu{}", i, cpu_id);
        }
    }
}

MsgManager::~MsgManager(){
    MutexLocker l(m_mutex);
    if(is_running){
        clear_before_stop();
        is_running = false;
    }
    for(auto worker : workers){
        if(worker->running()){
            worker->stop();
        }
        delete worker;
    }
    if(declare_msg){
        declare_msg->put();
        declare_msg = nullptr;
    }
    m_msger_cb = nullptr;
}

MsgWorker* MsgManager::get_lightest_load_worker(){
    if(workers.size() <= 0) return nullptr;
    int load = workers[0]->get_job_num(), index = 0;
    for(int i = 1;i < workers.size(); ++i){
        if(load > workers[i]->get_job_num()){
            load = workers[i]->get_job_num();
            index = i;
        }
    }
    return workers[index];
}

void MsgManager::add_lp(ListenPort *lp){
    for(auto &worker : workers){
        worker->add_event(lp);
    }
}

void MsgManager::del_lp(ListenPort *lp){
    for(auto &worker : workers){
        worker->del_event(lp->fd);
    }
}

void MsgManager::add_conn(Connection *conn){
    auto worker = get_lightest_load_worker();
    if(!worker)
        return;
    if(!conn->is_owner_fixed()){
        conn->set_owner(worker);
    }
    if(conn->has_fd()){
        worker->add_event(conn);
    }else{
        worker->update_job_num(1);
    }
}

void MsgManager::del_conn(Connection *conn){
    MsgWorker *worker = conn->get_owner();
    if(worker){
        if(conn->has_fd()){
            worker->del_event(conn->fd);
        }else{
            worker->update_job_num(-1);
        }
    }
}

ListenPort* MsgManager::add_listen_port(NodeAddr *addr, msg_ttype_t ttype){
    ML(mct, trace, "{} {}", addr->to_string(), msg_ttype_to_str(ttype));
    MutexLocker l(m_mutex);
    if(!addr) return nullptr;
    auto lp_iter = listen_map.find(addr);
    if(lp_iter != listen_map.end()){
        return lp_iter->second;
    }
    ListenPort *lp = nullptr;
    switch(ttype){
    case msg_ttype_t::TCP:
        lp = Stack::get_tcp_stack()->create_listen_port(addr);
        if(!lp) return nullptr;
        lp->set_listener(this);
        this->add_lp(lp);
        addr->get();
        listen_map[addr] = lp;
        break;
#ifdef HAVE_RDMA
    case msg_ttype_t::RDMA:
        {
            auto rdma_stack = Stack::get_rdma_stack();
            if(!rdma_stack) return nullptr;
            lp = rdma_stack->create_listen_port(addr);
        }
        if(!lp) return nullptr;
        lp->set_listener(this);
        this->add_lp(lp);
        addr->get();
        listen_map[addr] = lp;
        break;
#endif
    default:
        return nullptr;
    };
    
    return lp;
}

int MsgManager::del_listen_port(NodeAddr *addr){
    ML(mct, trace, "{}", addr->to_string());
    MutexLocker l(m_mutex);
    if(!addr) return -1;
    auto lp_iter = listen_map.find(addr);
    if(lp_iter != listen_map.end()){
        auto lp_addr = lp_iter->first;
        auto lp = lp_iter->second;
        listen_map.erase(lp_iter);
        lp_addr->put();
        this->del_lp(lp);
        return 1;
    }
    return 0;
}

Msg *MsgManager::get_declare_msg(){
    MutexLocker l(m_mutex);
    if(declare_msg){
        return declare_msg;
    }
    Msg *msg = Msg::alloc_msg(mct);
    msg->type = FLAME_MSG_TYPE_DECLARE_ID;
    msg_declare_id_d data;
    data.msger_id = mct->config->msger_id;
    NodeAddr *tcp_lp_addr = nullptr;
    NodeAddr *rdma_lp_addr = nullptr;
    for(auto &pair : listen_map){
        switch(pair.second->get_ttype()){
        case msg_ttype_t::TCP:
            if(!tcp_lp_addr){
                tcp_lp_addr = pair.first;
            }
            break;
        case msg_ttype_t::RDMA:
            if(!rdma_lp_addr){
                rdma_lp_addr = pair.first;
            }
            break;
        }
    }
    if(tcp_lp_addr){
        data.has_tcp_lp = true;
        tcp_lp_addr->encode(&data.tcp_listen_addr, 
                            sizeof(data.tcp_listen_addr));
    }
    if(rdma_lp_addr){
        data.has_rdma_lp = true;
        rdma_lp_addr->encode(&data.rdma_listen_addr, 
                            sizeof(data.rdma_listen_addr));
    }
    
    msg->append_data(data);
    declare_msg = msg;
    return declare_msg;
}

Connection* MsgManager::add_connection(NodeAddr *addr, msg_ttype_t ttype){
    if(!addr) return nullptr;
    ML(mct, trace, "{} {}", addr->to_string(), msg_ttype_to_str(ttype));
    Connection *conn = nullptr;
    switch(ttype){
    case msg_ttype_t::TCP:
        conn = Stack::get_tcp_stack()->connect(addr);
        if(!conn) return nullptr;
        conn->set_listener(this);
        this->add_conn(conn);
        break;
#ifdef HAVE_RDMA
    case msg_ttype_t::RDMA:
        {
            auto rdma_stack = Stack::get_rdma_stack();
            if(!rdma_stack) return nullptr;
            conn = rdma_stack->connect(addr);
        }
        if(!conn) return nullptr;
        conn->set_listener(this);
        this->add_conn(conn);
        break;
#endif
    default:
        return nullptr;
    };

    Msg *msg = get_declare_msg();
    conn->send_msg(msg);

    //conn->put();  //return conn with its ownership
    return conn;
}

int MsgManager::del_connection(Connection *conn){
    if(!conn) return -1;
    ML(mct, trace, "{}", conn->to_string());
    {
        MutexLocker l(m_mutex);
        if(session_unknown_conns.erase(conn) > 0){
            conn->put();
        }
        this->del_conn(conn);
    }
    if(conn->get_session()){
        conn->get_session()->del_conn(conn);
    }
    return 0;
}

Session *MsgManager::get_session(msger_id_t msger_id){
    ML(mct, trace, "{}", msger_id_to_str(msger_id));
    MutexLocker l(m_mutex);
    auto it = session_map.find(msger_id);
    if(it != session_map.end()){
        return it->second;
    }
    Session *s = new Session(mct, msger_id);
    session_map[msger_id] = s;
    return s;
}

int MsgManager::del_session(msger_id_t msger_id){
    ML(mct, trace, "{}", msger_id_to_str(msger_id));
    MutexLocker l(m_mutex);
    auto it = session_map.find(msger_id);
    if(it != session_map.end()){
        return -1;
    }
    it->second->put();
    session_map.erase(it);
    return 0;
}

int MsgManager::start(){
    ML(mct, trace, "");
    clear_done = false;
    MutexLocker l(m_mutex);
    if(is_running) return 0;
    is_running = true;
    for(auto &worker : workers){
        worker->start();
    }
    return 0;
    
}

int MsgManager::clear_before_stop(){
    ML(mct, trace, "");
    MutexLocker l(m_mutex);
    for(auto& pair : listen_map){
        del_lp(pair.second);
        pair.first->put();
        pair.second->put();
    }
    listen_map.clear();
    for(auto conn : session_unknown_conns){
        del_conn(conn);
        conn->put();
    }
    session_unknown_conns.clear();
    for(auto& pair : session_map){
        pair.second->put();
    }
    session_map.clear();
    clear_done = true;
    return 0;
}

int MsgManager::stop(){
    ML(mct, trace, "");
    MutexLocker l(m_mutex);
    if(!is_running) return 0;
    is_running = false;
    for(auto &worker : workers){
        worker->stop();
    }
    return 0;
}

void MsgManager::on_listen_accept(ListenPort *lp, Connection *conn){
    conn->get();
    ML(mct, trace, "{} {}", lp->to_string(), conn->to_string());
    {
        MutexLocker l(m_mutex);
        session_unknown_conns.insert(conn);
        conn->set_listener(this);
        this->add_conn(conn);
    }
    if(m_msger_cb){
        m_msger_cb->on_listen_accept(lp, conn);
    }
}

void MsgManager::on_listen_error(NodeAddr *listen_addr){
    listen_addr->get();
    ML(mct, trace, "{}", listen_addr->to_string());
    del_listen_port(listen_addr);
    if(m_msger_cb){
        m_msger_cb->on_listen_error(listen_addr);
    }
    listen_addr->put();
}

void MsgManager::on_conn_recv(Connection *conn, Msg *msg){
    ML(mct, trace, "{} {}", conn->to_string(), msg->to_string());
    if(msg->type == FLAME_MSG_TYPE_DECLARE_ID){
        msg_declare_id_d data;
        auto it = msg->data_buffer_list().begin();
        data.decode(it);
        Session* s = get_session(data.msger_id);
        assert(s);
        if(data.has_tcp_lp){
            auto tcp_listen_addr = new NodeAddr(mct, data.tcp_listen_addr);
            s->set_listen_addr(tcp_listen_addr, msg_ttype_t::TCP);
            tcp_listen_addr->put();
        }
        if(data.has_rdma_lp){
            auto rdma_listen_addr = new NodeAddr(mct, data.rdma_listen_addr);
            s->set_listen_addr(rdma_listen_addr, msg_ttype_t::RDMA);
            rdma_listen_addr->put();
        }
        //Alway success though the conn may has the same type and same sl.
        //Here may has duplicate conns. 
        int r = s->add_conn(conn);
        {
            MutexLocker l(m_mutex);
            if(session_unknown_conns.erase(conn) > 0){
                conn->put();
            }
        }
        //When the two sides connect to each other at the same time.
        //Here may drop both.
        //So, we just reserve both here(r == 0). There may be better solutions.
        if(r){ //add failed.(maybe duplicate)
            ML(mct, info, "Drop {} due to duplicae", conn->to_string());
            conn->close();
        }else if(m_msger_cb){
            m_msger_cb->on_conn_declared(conn, s);
        }
        
        return;
    }

    if(m_msger_cb){
        m_msger_cb->on_conn_recv(conn, msg);
    }
}

void MsgManager::on_conn_error(Connection *conn){
    ML(mct, trace, "{}", conn->to_string());
    if(m_msger_cb){
        m_msger_cb->on_conn_error(conn);
    }
    this->del_connection(conn);
}

} //namespace msg
} //namespace flame