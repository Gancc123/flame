#ifndef FLAME_MSG_SESSION_H
#define FLAME_MSG_SESSION_H

#include "msg/msg_context.h"
#include "common/thread/mutex.h"

#include "internal/types.h"
#include "msg_types.h"
#include "Connection.h"

#include <vector>

namespace flame{
namespace msg{

class Session : public RefCountedObject{

    NodeAddr *tcp_listen_addr = nullptr;
    NodeAddr *rdma_listen_addr = nullptr;

    struct conn_entry_t{
        msg_ttype_t ttype; // transport type
        uint8_t sl; // service level
        Connection *conn;
    };

    std::vector<conn_entry_t> conns;

    Mutex conns_mutex;
    Mutex lp_mutex;


public:
    explicit Session(MsgContext *c, msger_id_t peer)
    : RefCountedObject(c), peer_msger_id(peer), 
     conns_mutex(MUTEX_TYPE_ADAPTIVE_NP),
     lp_mutex(MUTEX_TYPE_ADAPTIVE_NP){
        conns.reserve(4);
    }

    ~Session(){
        {
            MutexLocker l(lp_mutex);
            if(tcp_listen_addr){
                tcp_listen_addr->put();
                tcp_listen_addr = nullptr;
            }
            if(rdma_listen_addr){
                rdma_listen_addr->put();
                rdma_listen_addr = nullptr;
            }
        }
        
        MutexLocker l(conns_mutex);
        for(auto entry : conns){
            entry.conn->set_session(nullptr);
            entry.conn->put();
        }
        conns.clear();
    }

    /**
     * @brief Session中的目的端监听地址是否已设置
     * @return bool
     */
    bool ready() const {
        return (tcp_listen_addr != nullptr) && (rdma_listen_addr != nullptr);
    }

    /**
     * @brief 获取指定类型的目的端监听端口ip/port信息
     * @param ttypr TCP或RDMA
     * @return ip/port信息
     */
    NodeAddr *get_listen_addr(msg_ttype_t ttype=msg_ttype_t::TCP){
        MutexLocker l(lp_mutex);
        switch(ttype){
        case msg_ttype_t::TCP:
            return tcp_listen_addr;
        case msg_ttype_t::RDMA:
            return rdma_listen_addr;
        default:
            return nullptr;
        }
    }

    /**
     * @brief 设置远端监听端口地址
     * @param na 由node_addr_t转型成的uint64_t 
     * @param ttype TCP或RDMA
     */
    void set_listen_addr(uint64_t na, msg_ttype_t ttype=msg_ttype_t::TCP){
        node_addr_t addr(na);
        NodeAddr *nap = new NodeAddr(mct, addr);
        nap->set_ttype((uint8_t)ttype);
        set_listen_addr(nap, ttype);
        nap->put();
    }
    /**
     * @brief 设置远端监听端口地址
     * @param addr NodeAddr地址
     * @param ttype TCP或RDMA
     */
    void set_listen_addr(NodeAddr *addr, msg_ttype_t ttype=msg_ttype_t::TCP){
        if(!addr) return;
        addr->get();
        MutexLocker l(lp_mutex);
        switch(ttype){
        case msg_ttype_t::TCP:
            if(tcp_listen_addr){
                tcp_listen_addr->put();
            }
            tcp_listen_addr = addr;
            break;
        case msg_ttype_t::RDMA:
            if(rdma_listen_addr){
                rdma_listen_addr->put();
            }
            rdma_listen_addr = addr;
            break;
        default:
            addr->put();
        }
    }

    /**
     * @brief 获取连接实例
     * 根据连接类型和服务等级获取连接实例
     * 如果连接不存在，将会创建连接
     * 一般情况下，同类型同sl的连接只有一条
     * 当通信两端同时主动建立连接时，连接可能存在两条
     * @param ttype TCP或RDMA
     * @param sl 服务等级
     * @return 连接实例
     */
    Connection *get_conn(msg_ttype_t ttype=msg_ttype_t::TCP, uint8_t sl=0);
    /**
     * @brief 向Session中添加连接实例
     * 一般不需要调用此函数
     * @param conn 连接实例
     * @param sl 服务等级
     * @return 0 添加成功
     * @return < 0 添加失败
     */
    int add_conn(Connection *conn, uint8_t sl=0);
    /**
     * @brief 从Session中删除连接实例
     * @param conn 连接实例
     * @return 0 删除成功
     * @return < 0 删除失败
     */
    int del_conn(Connection *conn);

    const msger_id_t peer_msger_id;

    std::string to_string() const;
};

} //namespace msg
} //namespace flame

#endif