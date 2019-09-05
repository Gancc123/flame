/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-04 15:34:03
 */
#ifndef FLAME_MSG_CONNECTION_H
#define FLAME_MSG_CONNECTION_H

#include <list>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <atomic>

#include "msg_common.h"
#include "Msg.h"
#include "MsgWorker.h"
#include "msg/event/event.h"

namespace flame{
namespace msg{

class Session;

class Connection : public EventCallBack{
    conn_id_t conn_id;
    Session *session;
    MsgWorker *owner;
    ConnectionListener *m_listener;
    std::atomic<bool> submit_pending;
public:
    explicit Connection(MsgContext *c)
    :EventCallBack(c, FLAME_EVENT_READABLE | FLAME_EVENT_WRITABLE),
     session(nullptr), owner(nullptr), m_listener(nullptr),
     submit_pending(false){
         
    }
    ~Connection() {};

    virtual msg_ttype_t get_ttype() = 0;

    /**
     * put msg to send_msg_list, and actually send if possible
     * @param msg: maybe be nullptr.
     * @param more: don't submit if true
     * @return: total bytes actually sended. when < 0, error.
     */
    virtual ssize_t send_msg(Msg *msg, bool more=false) = 0;
    virtual Msg* recv_msg() = 0;

    /**
     * put msgs to send_msg_list, and actually send if possible
     * @param msg: maybe be nullptr.
     * @return: total bytes actually sended. when < 0, error.
     */
    virtual ssize_t send_msgs(std::list<Msg *> &msgs){
        ssize_t total = 0;
        while(!msgs.empty()){
            ssize_t r = send_msg(msgs.front());
            if(r >= 0){
                total += r;
                msgs.pop_front();
            }else{
                total = -1;
                break;
            }
        }
        return total;
    }

    virtual void post_submit() {
        if(submit_pending || !owner) return;
        submit_pending = true;
        owner->post_work([this](){
            ML(this->mct, trace, "in post_submit()");
            submit_pending = false;
            this->send_msg(nullptr, false);
        });
    }

    /**
     * the number of msgs to be sended. 
     * @return: the size of send_msg_list
     */
    virtual int pending_msg() = 0;
    virtual bool is_connected() = 0;
    virtual void close() = 0;
    virtual void set_fd(int fd) { this->fd = fd; }
    virtual int get_fd() const { return this->fd; }
    /**
     * Some connection may have no fd.
     * return false, when don't need to add connection to poll loop.
     */
    virtual bool has_fd() const { return true; }
    /**
     * Some connection may set owner by itself.
     * return true, when don't need to set owner by msg_manager.
     */
    virtual bool is_owner_fixed() const { return false; }

    void set_id(conn_id_t id){
        this->conn_id = id;
    }

    conn_id_t get_id() const{
        return this->conn_id;
    }
    
    inline Session* get_session() const{
        return this->session; //* 这里获得的session可能已被释放
    }

    inline void set_session(Session *s){
        this->session = s;  //* 这里s->get()会造成循环引用
    }

    void set_owner(MsgWorker *worker){
        this->owner = worker;
    }

    MsgWorker *get_owner() const {
        return this->owner;
    }

    void set_listener(ConnectionListener *listener){
        this->m_listener = listener;
    }

    ConnectionListener* get_listener() const{
        return this->m_listener;
    }

    virtual void read_cb() override  {};
    virtual void write_cb() override {};
    virtual void error_cb() override {};

    virtual std::string to_string() const{
        auto s = fmt::format("[Conn {}]", this->conn_id.to_string());
        return s;
    }

};

} //namespace msg
} //namespace flame

#endif