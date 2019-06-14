#ifndef FLAME_MSG_SPDK_SPDK_MSG_WORKER_H
#define FLAME_MSG_SPDK_SPDK_MSG_WORKER_H

#include "acconfig.h"
#include "msg/MsgWorker.h"

#include <list>
#include <tuple>
#include <cassert>

#include "spdk/env.h"
#include "spdk/event.h"
#include "spdk/thread.h"

namespace flame{
namespace msg{

struct work_fn_container_t{
    work_fn_t fn;
};

struct poller_fn_container_t{
    poller_fn_t fn;
};

class SpdkMsgWorker : public MsgWorker{
    std::string name;
    uint32_t lcore;

    EventPoller event_poller;
    uint64_t event_poller_id;
    std::vector<FiredEvent> fevents;
    struct timeval event_poller_timeout_zero;

    std::atomic<uint32_t> next_poller_id;
    std::list<std::tuple<uint64_t, struct spdk_poller *, poller_fn_p, void *>>
                                                                poller_list;

    std::atomic<int> extra_job_num;
    std::atomic<bool> is_running;

    void add_poller(poller_fn_p poller_fn, void *arg, uint64_t poller_id);
    void del_poller(uint64_t poller_id);
    void destroy_poller(struct spdk_poller ** ppoller, poller_fn_p, void *);
public:
    explicit SpdkMsgWorker(MsgContext *c, int i);
    ~SpdkMsgWorker();

    virtual int set_affinity(int id) override;

    virtual msg_worker_type_t type() const override{
        return msg_worker_type_t::SPDK;
    }

    virtual int get_job_num() override;
    virtual void update_job_num(int v) override;

    virtual int get_event_num() override;
    virtual int add_event(EventCallBack *ecb) override;
    virtual int del_event(int fd) override;

    virtual void wakeup() override {};
    virtual bool am_self() const override{
        return spdk_env_get_current_core() == lcore;
    };

    virtual std::string get_name() const override{
        return name;
    }

    virtual uint64_t post_time_work(uint64_t microseconds, 
                                        work_fn_t work_fn) override{ return 0; }
    virtual void cancel_time_work(uint64_t time_work_id) override {};

    virtual uint64_t reg_poller(poller_fn_t poller_fn) override;
    virtual uint64_t reg_poller(poller_fn_p poller_fn, void *arg) override;
    virtual void unreg_poller(uint64_t poller_id) override;

    virtual void post_work(work_fn_t work) override;

    virtual void post_work(work_fn_p work_fn, void *arg1, void *arg2) override;

    virtual void start() override;
    virtual void stop() override;
    virtual bool running() const override{
        return is_running.load();
    }

    static void event_call_fn(void *arg1, void *arg2);

    static int poller_call_fn(void *ctx);
    
    static int event_poller_function(void *arg);
};


}//namespace msg
}//namespace flame

#endif //FLAME_MSG_SPDK_SPDK_MSG_WORKER_H