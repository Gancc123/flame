/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief: 消息模块上下文
 * @version: 0.1
 * @date: 2019-06-05
 * @copyright: Copyright (c) 2019
 * 
 * - MsgContext 消息模块上下文
 */
#ifndef FLAME_MSG_MSG_CONTEXT_H
#define FLAME_MSG_MSG_CONTEXT_H

#include "include/flame.h"
#include "common/context.h"
#include "common/thread/mutex.h"
#include "common/thread/cond.h"

namespace flame{
namespace msg{

class MsgConfig;
class MsgManager;
class MsgerCallback;
class MsgDispatcher;

enum class msg_module_state_t{
    INIT,
    RUNNING,
    CLEARING,
    CLEAR_DONE,
    FIN
};

typedef void (* msg_module_cb)(void *arg1, void *arg2);

struct MsgContext{
    FlameContext *fct;
    MsgConfig *config;
    MsgManager *manager;
    msg_module_state_t state;
    uint32_t bind_core; //only for SPDK mode.
    
    msg_module_cb clear_done_cb;
    void *clear_done_arg1;
    void *clear_done_arg2;

    msg_module_cb fin_cb;
    void *fin_arg1;
    void *fin_arg2;

    explicit MsgContext(FlameContext *c)
    : fct(c), config(nullptr), manager(nullptr),
      state(msg_module_state_t::INIT), bind_core(0),
      clear_done_cb(nullptr), fin_cb(nullptr), 
      thr_fin_mutex(), thr_fin_cond(thr_fin_mutex), thr_fin_ok(false) {};

    /**
     * @brief 加载配置文件
     * 加载之后可通过mct->config进行访问
     * @return 0 加载成功
     * @return -1 加载失败
     */
    int load_config();

    /**
     * @brief 初始化消息模块上下文
     * 建议在程序开始时初始化，并需要在SPDK环境初始化完成时初始化
     * @param msger_cb 消息模块回调接口实例
     * @return 0 初始化成功
     * @return -1 初始化失败
     */
    int init(MsgerCallback *msger_cb);

    /**
     * @brief 销毁消息模块上下文
     * 销毁过程分为数个阶段：
     * 1. 连接清理阶段(线程不能关闭)
     * 2. 执行clear_done_cb()
     * 3. 资源销毁阶段
     * 4. 执行fin_cb()
     * SPDK模式下，1和2之间是异步过程。
     * 
     * return 0 销毁成功
     */
    int fin();

    /**
     * @brief 用于消息模块内部通知连接清理完毕
     */
    void clear_done_notify();

    /**
     * @brief SPDK模式下在clear_done_cb()里调用
     * 用于最终销毁消息模块资源
     */
    void finally_fin();

//Only for thread mode.
private:
    Mutex thr_fin_mutex;
    Cond thr_fin_cond;
    bool thr_fin_ok;

    /**
     * @brief THREAD模式下销毁连接时的等待
     */
    void thr_fin_wait();

    /**
     * @brief THREAD模式下销毁连接完毕的通知
     */
    void thr_fin_signal();
};

} //namespace msg
} //namespace flame

#endif