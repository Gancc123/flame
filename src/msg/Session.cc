/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:17:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 15:17:04
 */
#include "Session.h"
#include "MsgManager.h"
#include "util/fmt.h"

#include <cstdlib>

namespace flame{
namespace msg{

Connection* Session::get_conn(msg_ttype_t ttype, uint8_t sl){
    MutexLocker l(conns_mutex);
    for(auto entry : conns){
        if(entry.ttype == ttype && entry.sl == sl){
            return entry.conn;
        }
    }

    auto msg_manager = mct->manager;
    auto addr = get_listen_addr(ttype);
    if(!addr){
        return nullptr;
    }

    //get conn with its ownership
    Connection *conn = msg_manager->add_connection(addr, ttype);

    //conn->get();
    conn_entry_t entry;
    entry.ttype = ttype;
    entry.sl = sl;
    entry.conn = conn;
    conn->set_session(this);
    conns.push_back(entry);
    
    return conn;
}

int Session::add_conn(Connection *conn, uint8_t sl){
    if(!conn) return -1;
    conn->get();
    conn_entry_t entry;
    entry.ttype = conn->get_ttype();
    entry.sl = sl;
    entry.conn = conn;
    conn->set_session(this);
    
    MutexLocker l(conns_mutex);
    for(auto e : conns){
        if(e.conn == conn){
            conn->put();
            return 0;
        }
    }
    conns.push_back(entry);
    return 0;
}

int Session::del_conn(Connection *conn){
    int result = -1;
    MutexLocker l(conns_mutex);
    for(auto it = conns.begin(); it != conns.end(); ++it){
        if(it->conn == conn){
            it = conns.erase(it);
            conn->set_session(nullptr);
            conn->put();
            result = 0;
            break;
        }
    }
    return result;
}

std::string Session::to_string() const {
    auto s = fmt::format("[Session {} {} {}]({:p})", 
                            msger_id_to_str(peer_msger_id),
                            tcp_listen_addr?tcp_listen_addr->to_string():"",
                            rdma_listen_addr?rdma_listen_addr->to_string():"",
                            (void *)this);
    return s;
}

} //namespace msg
} //namespace flame