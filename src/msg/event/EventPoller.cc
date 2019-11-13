#include <sys/epoll.h>
#include <sys/time.h>
#include <unistd.h>
#include <utility>

#include "EventPoller.h"
#include "msg/msg_def.h"
#include "msg/internal/errno.h"

namespace flame{
namespace msg{

EventPoller::EventPoller(MsgContext *c, int nevent)
:mct(c), events(new struct epoll_event[nevent]), size(nevent), ecb_map(),
 ecb_mutex(MUTEX_TYPE_DEFAULT), event_num(0){
    memset(events, 0, sizeof(struct epoll_event)*nevent);
    
    epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    if(epfd == -1){
        ML(mct, error, "unable to do epoll_create: {}", cpp_strerror(errno));
    }
}

EventPoller::~EventPoller(){
    close(epfd);
    delete []events;
    ecb_mutex.lock();
    for(std::pair<int, EventCallBack *> e: ecb_map){
        e.second->put();
    }
    ecb_mutex.unlock();
    mct = nullptr;
}

int EventPoller::set_event(int fd, EventCallBack *ecb){
    struct epoll_event ee;
    int op, new_mask;
    op = EPOLL_CTL_ADD;
    EventCallBack *old_ecb = nullptr;
    ecb->get();
    ecb_mutex.lock();
    auto ecb_iter = ecb_map.find(fd);
    if(ecb_iter != ecb_map.end()){
        if(ecb->mask != ecb_iter->second->mask){
            op = EPOLL_CTL_MOD;
        }
        old_ecb = ecb_iter->second;
    }
    ecb_map[fd] = ecb;
    ecb_mutex.unlock();
    // avoid wake up all threads when share fd in many thread.
    ee.events = EPOLLET; 
    if(ecb->mask & FLAME_EVENT_READABLE)
        ee.events |= EPOLLIN;
    if(ecb->mask & FLAME_EVENT_WRITABLE)
        ee.events |= EPOLLOUT;
    ee.data.u64 = 0;
    ee.data.ptr = (void *)ecb;
    if(epoll_ctl(epfd, op, fd, &ee) == -1){
        ecb->put();
        ecb_mutex.lock();
        if(old_ecb){
             ecb_map[fd] = old_ecb;
        }
        ecb_mutex.unlock();
        ML(mct, error, "epoll_ctl: set fd={} failed. {}", 
                                                    fd, cpp_strerror(errno));
        return -errno;
    }
    ++event_num;
    if(old_ecb){
        old_ecb->put();
    }
    return 0;
}

int EventPoller::del_event(int fd){
    int r = 0;
    struct epoll_event ee; //* ee没有任何作用，也不会返回删除的event
    if(epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &ee) < 0){
        ML(mct, error, "epoll_ctl: delete fd={} failed. {}", fd, 
                                                        cpp_strerror(errno));
        return -errno;
    }
    EventCallBack *ecb = nullptr;
    ecb_mutex.lock();
    auto ecb_iter = ecb_map.find(fd);
    if(ecb_iter != ecb_map.end()){
        ecb = ecb_iter->second;
        ecb_map.erase(ecb_iter);
    }else{
        r = -1;
    }
    ecb_mutex.unlock();
    --event_num;
    if(ecb){
        ecb->put();
    }
    return r;
}

int EventPoller::process_events(std::vector<FiredEvent> &fired_events, 
                                    struct timeval *tvp){
    int retval, numevents = 0;
    retval = epoll_wait(epfd, events, size, 
                        tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if(retval > 0){
        int i;
        numevents = retval;
        fired_events.resize(numevents);
        for(i = 0;i < numevents;i++){
            int mask = 0;
            struct epoll_event *e = events + i;

            if (e->events & EPOLLIN) mask |= FLAME_EVENT_READABLE;
            if (e->events & EPOLLOUT) mask |= FLAME_EVENT_WRITABLE;
            if (e->events & EPOLLERR) 
                mask |= FLAME_EVENT_ERROR;
            if (e->events & EPOLLHUP) 
                mask |= FLAME_EVENT_ERROR;
            fired_events[i].ecb = (EventCallBack *)e->data.ptr;
            fired_events[i].ecb->mask |= (FLAME_EVENT_ERROR & mask);
            fired_events[i].mask = mask;
        }
        
    }
    return numevents;
}

} //namespace msg
} //namespace flame
