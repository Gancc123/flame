/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief: 消息模块工作线程接口定义
 * @version: 0.1
 * @date: 2019-05-08
 * @copyright: Copyright (c) 2019
 * 
 * - MsgWorker 消息模块工作线程接口定义
 * - ThrMsgWorker pthread版消息模块工作线程实现
 */
#ifndef FLAME_MSG_MSG_WORKER_H
#define FLAME_MSG_MSG_WORKER_H

#include "msg_common.h"
#include "common/thread/mutex.h"
#include "common/thread/thread.h"
#include "msg/event/EventPoller.h"

#include <deque>
#include <map>
#include <atomic>
#include <functional>
#include <chrono>

namespace flame{
namespace msg{

typedef std::function<void(void)> work_fn_t;

//Return non-zero means that poller has processed some event.
typedef std::function<int(void)> poller_fn_t;

typedef void(* work_fn_p)(void *arg1, void *arg2);
typedef int(* poller_fn_p)(void *ctx);

enum class msg_worker_type_t{
    UNKNOWN = 0,
    THREAD = 1,
    SPDK = 2
};

class MsgWorker{
protected:
    int index;
    MsgContext *mct;
public:
    explicit MsgWorker(MsgContext *c, int i)
    : mct(c), index(i) {};
    virtual ~MsgWorker() {};

    /**
     * @brief 设置CPU亲和性
     * @param id CPU核心ID
     * @return 0 设置成功
     * @return < 0 设置失败
     */
    virtual int set_affinity(int id) = 0;

    /**
     * @brief 工作线程类型
     * @return THREAD(pthread)或SPDK(SPDK)
     */
    virtual msg_worker_type_t type() const { 
        return msg_worker_type_t::UNKNOWN; 
    }

    /**
     * @brief 线程绑定的监听端口、连接等的总数
     * 用于衡量该线程的负载情况
     * @return 线程绑定的监听端口、连接等的总数
     */
    virtual int get_job_num() = 0;
    /**
     * @brief 更新绑定的工作数量
     * @param v 可以为正值或负值
     */
    virtual void update_job_num(int v) = 0;

    /**
     * @brief 被监听的fd数量
     * @return int
     */
    virtual int get_event_num() = 0;
    /**
     * @brief 监听新的EventCallBack实例
     * 同个fd只会有1个fd，后设置的ecb会覆盖前面的ecb
     * @param ecb 包含fd和相应的回调函数
     * @return 0 设置成功
     * @return < 0 设置失败
     */
    virtual int add_event(EventCallBack *ecb) = 0;
    /**
     * @brief 删除对某EventCallBack的监听
     * @param fd EventCallBack对应的fd
     * @return 0 删除成功
     * @return < 0 删除失败
     */
    virtual int del_event(int fd) = 0;

    /**
     * @brief 唤醒工作线程
     */
    virtual void wakeup() = 0;
    /**
     * @brief 判断当前线程是否为此线程
     * @return bool
     */
    virtual bool am_self() const = 0;

    /**
     * @brief 消息模块工作线程内部id
     * @return int
     */
    int get_id() const { return index; }
    /**
     * @brief 消息模块工作线程字符串标识
     * @return std::string
     */
    virtual std::string get_name() const = 0; 

    /**
     * @brief 添加定时工作任务
     * 消息模块未使用此接口，可以不实现
     * @param microseconds 延后执行的微秒数
     * @param work_fn 工作任务
     * @return uint64_t 用于取消任务的定时工作任务id
     */
    virtual uint64_t post_time_work(uint64_t microseconds, 
                                    work_fn_t work_fn) = 0;
    /**
     * @brief 取消定时工作任务
     * 当定时工作任务已经开始执行时，无法取消
     * @param time_work_id 用于标识定时工作任务的id
     */            
    virtual void cancel_time_work(uint64_t time_work_id) = 0;

    /**
     * @brief 添加工作任务
     * @param work 工作任务
     */
    virtual void post_work(work_fn_t work) = 0;
    /**
     * @brief 添加工作任务
     * @param work_fn 工作任务函数指针
     * @param arg1 工作任务函数参数1
     * @param arg2 工作任务函数参数2
     */
    virtual void post_work(work_fn_p work_fn, void *arg1, void *arg2){
        post_work([work_fn, arg1, arg2](){
            work_fn(arg1, arg2);
        });
    }

    /**
     * @brief 注册轮询器
     * 轮询器会被不停地循环调用
     * @param poller_fn 轮询器轮询函数
     * @return 轮询器id
     */
    virtual uint64_t reg_poller(poller_fn_t poller_fn) = 0;
    /**
     * @brief 注册轮询器
     * @param 轮询器函数指针
     * @param 轮询器函数参数
     * @return 轮询器id
     */
    virtual uint64_t reg_poller(poller_fn_p poller_fn, void *arg){
        return reg_poller([poller_fn, arg]()->int{
            return poller_fn(arg);
        });
    }
    /**
     * @brief 注销轮询器
     * @param 轮询器id
     */
    virtual void unreg_poller(uint64_t poller_id) = 0;

    /**
     * @brief 消息模块工作线程开始执行
     */
    virtual void start() = 0;
    /**
     * @brief 消息模块工作线程停止执行
     */
    virtual void stop() = 0;
    
    /**
     * @brief 消息模块工作线程是否正在运行
     */ 
    virtual bool running() const = 0;
};

class ThrMsgWorker;

class MsgWorkerThread : public Thread{
    ThrMsgWorker *worker;
public:
    explicit MsgWorkerThread(ThrMsgWorker *w)
    : worker(w) {};
    ~MsgWorkerThread() override{};
protected:
    virtual void entry() override;
};

struct HandleNotifyCallBack : public EventCallBack{
    HandleNotifyCallBack(MsgContext *c)
    : EventCallBack(c, FLAME_EVENT_READABLE){}
    virtual void read_cb() override;
};

class ThrMsgWorker : public MsgWorker{
    using time_point = std::chrono::steady_clock::time_point;
    using clock_type = std::chrono::steady_clock;
    std::string name;

    EventPoller event_poller;

    std::atomic<uint64_t> next_id;
    std::multimap<time_point, std::pair<uint64_t, work_fn_t>> time_works;
    std::map<uint64_t, 
            std::multimap<time_point, std::pair<uint64_t, work_fn_t>>::iterator
            > tw_map;

    std::list<std::pair<uint64_t, poller_fn_t>> poller_list;

    Mutex external_mutex;
    std::deque<work_fn_t> external_queue;
    std::atomic<int> external_num;
    std::atomic<bool> is_running; 

    std::atomic<int> extra_job_num;

    int notify_receive_fd;
    int notify_send_fd;

    MsgWorkerThread worker_thread;

    /**
     * @brief 添加定时工作任务
     * 非线程安全，需要在对应工作线程中执行。
     * @param expire 定时工作任务延后执行的时间
     * @param work_fn 工作任务函数
     * @param id 定时工作任务id
     */
    void add_time_work(time_point expire, work_fn_t work_fn, uint64_t id);
    /**
     * @brief 删除定时工作任务
     * 非线程安全，需要在对应工作线程中执行
     * @param time_work_id 定时工作任务id
     */
    void del_time_work(uint64_t time_work_id);
    /**
     * @brief 添加轮询器
     * 非线程安全，需要在对应工作线程中执行
     * @param poller_fn 轮询器函数
     * @param poller_id 轮询器id
     */
    void add_poller(poller_fn_t poller_fn, uint64_t poller_id);
    /**
     * @brief 删除轮询器
     * 非线程安全，需要在对应工作线程中执行
     * @param poller_id 轮询器id
     */
    void del_poller(uint64_t poller_id);
    /**
     * @brief 处理定时工作任务
     * 非线程安全，需要在对应工作线程中执行
     * @return 本次处理定时工作任务的数量
     */
    int  process_time_works();
    /**
     * @brief 执行一个轮询器
     * 当有多个轮询器时，该函数每次执行不同的轮询器，按轮转的方式选择
     * @return 本次轮询器处理的事件个数
     */
    int  iter_poller();

    /**
     * @brief 工作线程处理主过程
     */
    void process();
    /**
     * @brief 工作线程停止时，尽可能执行其中的工作任务
     */
    void drain();

    /**
     * @brief 将指定fd设置为非阻塞
     * @param sd 指定fd(文件描述符)
     * @return 0 设置成功
     * @return < 0 设置失败
     */
    int set_nonblock(int sd);
public:
    friend class MsgWorkerThread;

    explicit ThrMsgWorker(MsgContext *c, int i);
    ~ThrMsgWorker();

    /**
     * @brief 设置pthread的CPU亲和性
     * @param id CPU核心id
     * @return 0 设置成功
     * @return < 0 设置失败
     */
    virtual int set_affinity(int id) override;

    /**
     * @brief 返回工作线程类型
     * @return 固定返回THREAD(pthread类型)
     */
    virtual msg_worker_type_t type() const override{
        return msg_worker_type_t::THREAD;
    }

    /**
     * @brief 线程绑定的监听端口、连接等的总数
     * 用于衡量该线程的负载情况
     * @return 线程绑定的监听端口、连接等的总数
     */
    virtual int get_job_num() override;
    /**
     * @brief 更新绑定的工作数量
     * @param v 可以为正值或负值
     */
    virtual void update_job_num(int v) override;

    /**
     * @brief 被监听的fd数量
     * @return int
     */
    virtual int get_event_num() override;
    /**
     * @brief 监听新的EventCallBack实例
     * 同个fd只会有1个fd，后设置的ecb会覆盖前面的ecb
     * @param ecb 包含fd和相应的回调函数
     * @return 0 设置成功
     * @return < 0 设置失败
     */
    virtual int add_event(EventCallBack *ecb) override;
    /**
     * @brief 删除对某EventCallBack的监听
     * @param fd EventCallBack对应的fd
     * @return 0 删除成功
     * @return < 0 删除失败
     */
    virtual int del_event(int fd) override;

    /**
     * @brief 唤醒工作线程
     */
    virtual void wakeup() override;
    /**
     * @brief 判断当前线程是否为此线程
     * @return bool
     */
    virtual bool am_self() const override{
        return worker_thread.am_self();
    }

    /**
     * @brief 消息模块工作线程字符串标识
     * @return std::string
     */
    virtual std::string get_name() const override{
        return name;
    }

    /**
     * @brief 添加定时工作任务
     * 消息模块未使用此接口，可以不实现
     * 可以在任意线程调用
     * @param microseconds 延后执行的微秒数
     * @param work_fn 工作任务
     * @return uint64_t 用于取消任务的定时工作任务id
     */
    virtual uint64_t post_time_work(uint64_t microseconds, 
                                    work_fn_t work_fn) override;
    
    /**
     * @brief 取消定时工作任务
     * 当定时工作任务已经开始执行时，无法取消
     * 可以在任意线程调用
     * @param time_work_id 用于标识定时工作任务的id
     */    
    virtual void cancel_time_work(uint64_t time_work_id) override;

    /**
     * @brief 注册轮询器
     * 轮询器会被不停地循环调用
     * 注册轮询器后，线程将不再休眠
     * 可以在任意线程调用
     * @param poller_fn 轮询器轮询函数
     * @return 轮询器id
     */
    virtual uint64_t reg_poller(poller_fn_t poller_fn) override;
    /**
     * @brief 注销轮询器
     * 所有轮询器均被删除后，线程可在空闲时休眠
     * 可以在任意线程调用
     * @param 轮询器id
     */
    virtual void unreg_poller(uint64_t poller_id) override;

    /**
     * @brief 向工作线程添加工作任务
     * 可以在任意线程调用
     * @param work 工作任务
     */
    virtual void post_work(work_fn_t work) override;

    /**
     * @brief 启动工作线程
     */
    virtual void start() override;
    /**
     * @brief 停止工作线程
     */
    virtual void stop() override;
    /**
     * @brief 工作线程是否运行
     * @return bool
     */
    virtual bool running() const override{
        return is_running.load();
    }

};

} //namespace msg
} //namespace flame


#endif