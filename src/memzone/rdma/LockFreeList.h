#ifndef FLAME_MEMZONE_RDMA_LOCK_FREE_LIST_H
#define FLAME_MEMZONE_RDMA_LOCK_FREE_LIST_H

#include <atomic>
#include <functional>

namespace flame{
namespace memory{
namespace ib{

typedef std::function<void(void *)> lfl_del_cb_t;

struct lfl_elem_t{
    void *p;
    std::atomic<lfl_elem_t *> next;
};

class LockFreeList{
    lfl_elem_t head;
    lfl_del_cb_t del_cb;
public:
    explicit LockFreeList(lfl_del_cb_t dc)
    : del_cb(dc){
        head.p = nullptr;
        head.next = nullptr;
    }

    //assume that destruct when no thread use this.
    ~LockFreeList(){
        clear();
    }

    void push_back(void *p){
        lfl_elem_t *it = &head;
        lfl_elem_t *next = nullptr;

        lfl_elem_t *new_elem = new lfl_elem_t();
        assert(new_elem);
        new_elem->p = p;
        new_elem->next = nullptr;
        
        //assume that no delete operation.
        do{
            while((next = it->next.load(std::memory_order_consume)) 
                  != nullptr ){
                it = next;
            }
        }while(!it->next.compare_exchange_weak(next, new_elem,
                                                std::memory_order_release,
                                                std::memory_order_relaxed));
        //ensure that new_elem init done before CAS.
    }

    //assume that clear when no thread use this.
    void clear() {
        lfl_elem_t *it = head.next;
        while(it){
            del_cb(it->p);
            auto tmp = it;
            it = it->next;
            delete tmp;
        }
        head.next = nullptr;
    }

    lfl_elem_t *elem_iter() const{
        return head.next;
    }

};

}// namespace ib
}// namespace memory
}// namespace flame

#endif //FLAME_MEMZONE_RDMA_LOCK_FREE_LIST_H