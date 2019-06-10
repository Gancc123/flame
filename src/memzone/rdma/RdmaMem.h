#ifndef FLAME_MEMZONE_RDMA_RDMA_MEM_H
#define FLAME_MEMZONE_RDMA_RDMA_MEM_H

#include <cassert>
#include <map>
#include <infiniband/verbs.h>

#include "common/thread/mutex.h"
#include "msg/msg_common.h"
#include "msg/rdma/Infiniband.h"

#include "memzone/rdma/BuddyAllocator.h"
#include "memzone/rdma/LockFreeList.h"
#include "memzone/rdma/memory_conf.h"

namespace flame{
namespace memory{
namespace ib{

class RdmaBufferAllocator;

struct rdma_mem_header_t{
    uint32_t lkey;
    uint32_t rkey;
    size_t   size;
};

class RdmaBuffer{
    uint32_t m_lkey;
    uint32_t m_rkey;
    size_t   m_size;
    char * m_buffer;
    BuddyAllocator *m_allocator;
public:
    explicit RdmaBuffer(uint32_t rkey, uint64_t addr, size_t len)
    :m_rkey(rkey), m_buffer(reinterpret_cast<char *>(addr)), data_len(len),
      m_allocator(nullptr) {};
    explicit RdmaBuffer(void *ptr, BuddyAllocator *a);
    uint32_t lkey() const { return m_lkey; }
    uint32_t rkey() const { return m_rkey; }
    size_t   size() const { return m_size; }
    char *   buffer() const { return m_buffer; }
    uint64_t addr() const { return reinterpret_cast<uint64_t>(m_buffer); }
    BuddyAllocator *allocator() const { return m_allocator; }

    void   set_rkey(uint32_t rkey) { m_rkey = rkey; }
    void   set_addr(uint64_t addr) { m_buffer = reinterpret_cast<char *>(addr);}
    size_t data_len = 0;
};

class RdmaMemSrc : public MemSrc{
    RdmaBufferAllocator *allocator_ctx;
    ibv_mr *mr;
public:
    explicit RdmaMemSrc(RdmaBufferAllocator *c) : allocator_ctx(c) {}
    virtual void *alloc(size_t s) override;
    virtual void free(void *p) override;
    virtual void *prep_mem_before_return(void *p, void *base, size_t size) 
                                                                    override;
};

class RdmaBufferAllocator{
    FlameContext *fct;
    MemoryConfig *mem_cfg;
    flame::msg::ib::ProtectionDomain *pd;
    LockFreeList lfl_allocators;
    uint8_t min_level;
    uint8_t max_level;

    static Mutex &mutex_of_allocator(BuddyAllocator *a){
        return *(reinterpret_cast<Mutex *>(a->extra_data));
    }
    static void delete_cb(void *p){
        BuddyAllocator *a = reinterpret_cast<BuddyAllocator *>(p);
        auto mem_src = a->get_mem_src();
        delete a;
        delete mem_src;
    }
    int expand();
    // size_t shrink();
public:
    explicit RdmaBufferAllocator(FlameContext *c, MemoryConfig *cfg, 
                                    flame::msg::ib::ProtectionDomain *p) 
    : fct(c), mem_cfg(cfg), pd(p),
      lfl_allocators(RdmaBufferAllocator::delete_cb) { 
    }

    int init();
    int fin();

    RdmaBuffer *alloc(size_t s);
    void        free(RdmaBuffer *buf);

    int  alloc_buffers(size_t s, int cnt, std::vector<RdmaBuffer*> &b);
    void free_buffers(std::vector<RdmaBuffer*> &b);

    size_t get_mem_used() const;
    size_t get_mem_reged() const;
    int get_mr_num() const;

    FlameContext *get_fct() const { return fct; }
    MemoryConfig *get_mem_cfg() const { return mem_cfg; }
    flame::msg::ib::ProtectionDomain *get_pd() const { return pd; }
};

} //namespace ib
} //namespace memory
} //namespace flame

#endif //FLAME_MEMZONE_RDMA_RDMA_MEM_H