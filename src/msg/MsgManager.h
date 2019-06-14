/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief: 消息模块管理器
 * @version: 0.1
 * @date: 2019-05-08
 * @copyright: Copyright (c) 2019
 * 
 * - MsgManager 管理消息模块的所有连接，监听端口，会话和消息模块工作线程
 */
#ifndef FLAME_MSG_MSG_MANAGER_H
#define FLAME_MSG_MSG_MANAGER_H

#include "internal/node_addr.h"
#include "internal/types.h"
#include "msg_types.h"
#include "Session.h"
#include "MsgWorker.h"

#include <vector>
#include <set>
#include <map>

namespace flame{
namespace msg{

class MsgManager : public ListenPortListener, public ConnectionListener{
    MsgContext *mct;
    std::map<NodeAddr *, ListenPort *> listen_map;
    std::map<msger_id_t, Session *, msger_id_comparator> session_map;
    std::vector<MsgWorker *> workers;
    MsgerCallback *m_msger_cb;
    std::atomic<bool> is_running;
    std::atomic<bool> clear_done;

    std::set<Connection *> session_unknown_conns;
    Msg *declare_msg = nullptr;

    Mutex m_mutex;

public:
    explicit MsgManager(MsgContext *c, int worker_num=4);

    ~MsgManager();

    /**
     * @brief 设置MsgerCallback回调实例
     */
    void set_msger_cb(MsgerCallback *msger_cb){
        this->m_msger_cb = msger_cb;
    }

    /**
     * @brief 获取MsgerCallback回调实例
     * @return MsgerCallback *
     */
    MsgerCallback *get_msger_cb() const{
        return this->m_msger_cb;
    }

    /**
     * @brief 获取消息模块工作线程个数
     * SPDK模式下消息模块工作线程会绑定到SPDK线程下
     * @return int
     */
    int get_worker_num() const {
        return workers.size();
    }

    /**
     * @brief 获取指定的MsgWorker
     * @param index MsgWorker的数组位置
     * @return MsgWorker *
     */
    MsgWorker *get_worker(int index){
        if(index < 0 || index >= workers.size()){
            return nullptr;
        }
        return workers[index];
    }

    /**
     * @brief 获取负载最轻的MsgWorker
     * @return MsgWorker *
     */
    MsgWorker *get_lightest_load_worker();

    /**
     * @brief 添加指定ip/port和传输类型(TCP/RDMA)的监听端口
     * @param addr ip/port信息
     * @param ttype TCP或RDMA
     * @return ListenPort实例，失败时返回null
     */
    ListenPort *add_listen_port(NodeAddr *addr, msg_ttype_t ttype);

    /**
     * @brief 删除指定ip/port的监听端口
     * @param addr ip/port信息
     * @return 1 删除成功
     * @return 0 未找到匹配端口
     * @return -1 删除出错
     */
    int del_listen_port(NodeAddr *addr);

    /**
     * @brief 建立连接
     * 不需要主动调用，Session->get_conn()会调用此函数
     * @param addr 目标ip/port信息
     * @param ttype TCP或RDMA
     * @return Connection实例，失败时返回null
     */
    Connection* add_connection(NodeAddr *addr, msg_ttype_t ttype);

    /**
     * @brief 删除连接
     * 删除连接时会附带删除从Session中移除此连接
     * @param conn Connection实例
     * @return 0 删除成功
     * @return -1 删除失败
     */
    int del_connection(Connection* conn);

    /**
     * @brief 获取Session
     * 不存在时，会新建新的Session实例
     * @param msger_id 全局唯一的msger_id，用于标识不同的通信实例
     * @return Session实例
     */
    Session *get_session(msger_id_t msger_id);

    /**
     * @brief 删除Session
     * @param msger_id 全局唯一的msger_id，用于标识不同的通信实例
     * @return 0 删除成功
     * @return -1 删除失败
     */
    int del_session(msger_id_t msger_id);

    /**
     * @brief 启动消息模块工作线程
     * @return 0 始终返回成功
     */
    int start();

    /**
     * @brief 在工作线程停止前执行清理工作
     * @return 0 始终返回成功
     */
    int clear_before_stop();

    /**
     * @brief 停止消息模块工作线程
     * SPDK模式下，SPDK线程可能尚未停止，消息模块的部分工作可能仍会执行
     * @return 0 始终返回成功
     */
    int stop();

    /**
     * @brief 是否清理完毕
     */
    bool is_clear_done() const { return this->clear_done; }

    /**
     * @brief 是否正在运行 
     */
    bool running() const { return this->is_running; }

    /**
     * @brief 当监听端口接收到连接建立请求时回调
     * 此函数中会回调MsgerCallback->on_listen_accept()
     * @param lp 监听端口
     * @param conn 新建立的连接
     */
    virtual void on_listen_accept(ListenPort *lp, Connection *conn) override;

    /**
     * @brief 当监听端口出错时回调
     * 1. 删除对应的listen_port
     * 2. 回调MsgerCallback->on_listen_error()
     * @param listen_addr 出错的监听地址
     */
    virtual void on_listen_error(NodeAddr *listen_addr) override;

    /**
     * @brief 当连接接收到消息时，回调此函数
     * 此函数中会回调MsgerCallback->on_conn_recv()
     * 或MsgerCallback->on_conn_declared()
     * 
     * @param conn 接收到消息的连接
     * @param msg 接收到消息实例
     */
    virtual void on_conn_recv(Connection *, Msg *) override;

    /**
     * @brief 当连接出错时回调此函数
     * 1. 回调MsgerCallback->on_conn_error()
     * 2. 删除出错的连接实例
     * @param conn 出错的连接实例
     */
    virtual void on_conn_error(Connection *conn) override;

private:
    /**
     * @brief 获取声明消息，包含msger_id和监听端口等信息
     * 消息在第一次初始化后，重复使用
     * @return 消息实例
     */
    Msg *get_declare_msg();

    /**
     * @brief 添加监听端口
     * 每个消息模块工作线程都会监听相同端口
     */
    void add_lp(ListenPort *lp);

    /**
     * @brief 删除监听端口
     * 从每个消息模块工作线程删除监听端口
     */
    void del_lp(ListenPort *lp);

    /**
     * @brief 将连接添加给某个消息模块工作线程
     */
    void add_conn(Connection *conn);

    /**
     * @brief 将连接从消息模块工作线程移除
     */
    void del_conn(Connection *conn);
};

} //namespace msg
} //namespace flame

#endif