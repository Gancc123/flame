/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief: 消息模块接口定义
 * @version: 0.1
 * @date: 2019-05-16
 * @copyright: Copyright (c) 2019
 * 
 * - ListenPortListener 监听端口回调接口定义
 * - ConnectionListener 连接回调接口定义 
 * - MsgData 消息数据接口定义
 * - MsgerCallback 消息模块回调接口定义
 */
#ifndef FLAME_MSG_MSG_TYPES_H
#define FLAME_MSG_MSG_TYPES_H

#include "internal/msg_buffer_list.h"
#include <vector>

namespace flame{
namespace msg{

/**
 * interface
 */
class ListenPort;
class Connection;
class NodeAddr;
class Msg;
class Session;
class RdmaRecvWr;

class ListenPortListener{
public:
    /**
     * @brief 当接收到连接建立请求时
     * @param ListenPort 监听端口实例
     * @param conn 新建立的连接
     */
    virtual void on_listen_accept(ListenPort *, Connection *conn) = 0;

    /**
     * @brief 当监听端口出错时
     * @param listen_addr 监听地址
     */
    virtual void on_listen_error(NodeAddr *listen_addr) = 0;
};

class ConnectionListener{
public:
    /**
     * @brief 当连接接收到到消息时
     * RdmaConnectionV2模式下，几乎不会用到
     * @param Connection 消息关联的连接
     * @param Msg 接收到的消息实例
     */
    virtual void on_conn_recv(Connection *, Msg *) = 0;

    /**
     * @brief 当连接出错时
     * @param conn 出错的连接
     */
    virtual void on_conn_error(Connection *conn) = 0;
};

struct MsgData{
    /**
     * @brief 将消息内容编码到MsgBufferList中
     * @param bl 编码数据的目标存储区域
     * @return 编码的字节数
     */
    virtual int encode(MsgBufferList &bl) = 0;

    /**
     * @brief 将消息内容从MsgBufferList的迭代器中解码出来
     * @param  it MsgBufferList迭代器，用于读取数据
     * @return 读取的字节数
     */
    virtual int decode(MsgBufferList::iterator &it) = 0;

    /**
     * @brief 消息编码后所占的数据长度
     * @return 消息编码后的字节数
     */
    virtual size_t size() { return 0; }
    virtual ~MsgData() {};
};

class MsgerCallback{
public:
    virtual ~MsgerCallback() {};
    /**
     * @brief 当接收到连接建立请求时
     * @param ListenPort 监听端口实例
     * @param conn 新建立的连接
     */
    virtual void on_listen_accept(ListenPort *lp, Connection *conn) {};

     /**
     * @brief 当监听端口出错时
     * @param listen_addr 监听地址
     */
    virtual void on_listen_error(NodeAddr *listen_addr) {};

    /**
     * @brief 当连接出错时
     * @param conn 出错的连接
     */
    virtual void on_conn_error(Connection *conn) {};

    /**
     * @brief 当连接声明自己的身份时
     * @param Connection 连接实例
     * @param Session 连接所属的会话
     */
    virtual void on_conn_declared(Connection *conn, Session *s) {};
    
    /**
     * @brief 当连接接收到到消息时
     * RdmaConnectionV2模式下，几乎不会用到
     * @param Connection 消息关联的连接
     * @param Msg 接收到的消息实例
     */
    virtual void on_conn_recv(Connection *, Msg *) {};

    /**
     * @brief 当RDMA ENV初始化完成时回调
     */
    virtual void on_rdma_env_ready() {};

    /**
     * @brief 为消息模块提供指定数量的RdmaRecvWr
     * 仅对RdmaConnectionV2生效
     * @param n 需要的RdmaRecvWr数量
     * @param wrs 存放RdmaRecvWr的目标数组
     * @return >= 0 返回RdmaRecvWr的数目
     */
    virtual int get_recv_wrs(int n, std::vector<RdmaRecvWr *> &wrs) { return 0;}
};

} //namespace msg
} //namespace flame

#endif 