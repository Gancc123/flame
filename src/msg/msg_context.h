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

    int load_config();

    int init(MsgerCallback *msger_cb);
    int fin();
    void clear_done_notify();
    void finally_fin();

//Only for thread mode.
private:
    Mutex thr_fin_mutex;
    Cond thr_fin_cond;
    bool thr_fin_ok;
    void thr_fin_wait();
    void thr_fin_signal();
};

} //namespace msg
} //namespace flame

#endif