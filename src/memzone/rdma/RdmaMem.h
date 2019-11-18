/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-06 15:04:27
 * @LastEditors: lwg
 * @LastEditTime: 2019-11-15 17:32:20
 */
#ifndef FLAME_MEMZONE_RDMA_RDMA_MEM_H
#define FLAME_MEMZONE_RDMA_RDMA_MEM_H

#include "include/buffer.h"

#include "common/thread/mutex.h"
#include "msg/msg_common.h"
#include "msg/rdma/Infiniband.h"

#include "memzone/rdma/BuddyAllocator.h"
#include "memzone/rdma/LockFreeList.h"
#include "memzone/rdma/memory_conf.h"

#include <cassert>
#include <map>
#include <infiniband/verbs.h>

namespace flame{
namespace memory{
namespace ib{

struct rdma_mem_header_t{
    uint32_t lkey;
    uint32_t rkey;
    size_t   size;
};

class RdmaBufferAllocator;

class RdmaBuffer : public Buffer{
    BuddyAllocator *buddy_allocator_;
    RdmaBufferAllocator* rdma_buffer_allocator_;
public:
    explicit RdmaBuffer(BuddyAllocator* buddy_allocator, uint64_t addr, size_t size, uint32_t rkey, uint32_t lkey, BufferTypes buffer_type)
    :Buffer(addr, size, rkey, lkey, buffer_type), buddy_allocator_(buddy_allocator) {};
    explicit RdmaBuffer(void *ptr, BuddyAllocator *a, RdmaBufferAllocator* rdma_buffer_allocator);
    virtual ~RdmaBuffer();
    char *   buffer() const { return (char*)addr_; }
    BuddyAllocator *get_buddy_allocator() const { return buddy_allocator_; }

    size_t data_len = 0;   //记录发送的数据长度，与size不同，size是整个buffer长度，不一定整个buffer都需要发送
};

class RdmaBufferAllocator : public BufferAllocator{
    MemoryConfig *mem_cfg_;
    flame::msg::ib::ProtectionDomain *pd_;
    LockFreeList lfl_allocators_;
    uint8_t min_level_;
    uint8_t max_level_;

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
public:
    static BufferAllocator* get_buffer_allocator(flame::msg::ib::ProtectionDomain *pd = nullptr, MemoryConfig *_cfg = nullptr){
        if(g_rdma_buffer_allocator == nullptr){
            RdmaBufferAllocator::g_rdma_buffer_allocator = new RdmaBufferAllocator(pd, _cfg);
        }
        return RdmaBufferAllocator::g_rdma_buffer_allocator;
    }
    static RdmaBufferAllocator* g_rdma_buffer_allocator;

    explicit RdmaBufferAllocator(flame::msg::ib::ProtectionDomain *pd, MemoryConfig *mem_cfg) 
    : BufferAllocator(), mem_cfg_(mem_cfg), pd_(pd), lfl_allocators_(RdmaBufferAllocator::delete_cb) { 
        init();
    }

    ~RdmaBufferAllocator(){
        fin();
    }
    int init();
    int fin();

    virtual int type() const override;
    virtual size_t max_size() const override;
    virtual size_t min_size() const override;
    virtual Buffer allocate(size_t sz) override;
    virtual Buffer* allocate_ptr(size_t sz) override;

    RdmaBuffer *alloc(size_t s);
    void        free(RdmaBuffer *buf);

    int  alloc_buffers(size_t s, int cnt, std::vector<RdmaBuffer*> &b);
    void free_buffers(std::vector<RdmaBuffer*> &b);

    size_t get_mem_used() const;
    size_t get_mem_reged() const;
    int get_mr_num() const;

    MemoryConfig *get_mem_cfg() const { return mem_cfg_; }
    flame::msg::ib::ProtectionDomain *get_pd() const { return pd_; }
};

class RdmaMemSrc : public MemSrc{   //此结构用于真正申请内存
    RdmaBufferAllocator *allocator_ctx;
    ibv_mr *mr;
public:
    explicit RdmaMemSrc(RdmaBufferAllocator *c) : allocator_ctx(c) {}
    virtual void *alloc(size_t s) override;
    virtual void free(void *p) override;
    virtual void *prep_mem_before_return(void *p, void *base, size_t size) 
                                                                    override;
};

} //namespace ib
} //namespace memory
} //namespace flame

#endif //FLAME_MEMZONE_RDMA_RDMA_MEM_H