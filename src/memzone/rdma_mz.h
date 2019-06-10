#ifndef FLAME_MEMZONE_RDMAMZ_H
#define FLAME_MEMZONE_RDMAMZ_H

#include "include/buffer.h"
#include "common/context.h"
#include "msg/rdma/Infiniband.h"

#include "memzone/mz_types.h"
#include "memzone/rdma/RdmaMem.h"

#include <cstdlib>

using namespace flame;
using namespace flame::memory;
using namespace flame::memory::ib;

namespace flame {
class RdmaAllocator;

class RdmaAllocator : public BufferAllocator {
private:
    RdmaBufferAllocator *allocator_ctx;
    void init(FlameContext *c, flame::msg::ib::ProtectionDomain *pd, MemoryConfig *_cfg);

public:
    RdmaAllocator(FlameContext *c, flame::msg::ib::ProtectionDomain *pd, MemoryConfig *_cfg) {
        init(c, pd, _cfg);
    }
    ~RdmaAllocator();

    RdmaBufferAllocator *get_allocator_ctx() const { return allocator_ctx; }

    virtual Buffer allocate(size_t sz) override;
    virtual int type() const override { return BufferTypes::BUFF_TYPE_RDMA; }
    virtual size_t max_size() const override;
    virtual size_t min_size() const override;
    virtual size_t free_size() const override;
    virtual bool empty() const override;
};

class RdmaBufferPtr : public BufferPtr {
private:
    RdmaBuffer *rb;
    RdmaAllocator *allocator;

    RdmaBufferPtr():rb(nullptr), allocator(nullptr) {
        
    }

    RdmaBufferPtr(RdmaBuffer *_rb, 
                    RdmaAllocator *_allocator):rb(_rb), 
                    allocator(_allocator) {

    }

public:
    virtual ~RdmaBufferPtr() {
        if(allocator && allocator->get_allocator_ctx()) {
            //allocator->mmgr->get_rdma_allocator()->free(rb);
            //std::cout << "free buffer" << std::endl;
            allocator->get_allocator_ctx()->free(rb);
        }
    }

    virtual void *addr() const override { return rb->buffer(); }
    virtual size_t size() const override { return rb->size(); }
    virtual int type() const { return BufferTypes::BUFF_TYPE_RDMA; }
    
    virtual bool resize(size_t sz) override {

    }

    uint32_t lkey() const { return rb->lkey();}
    uint32_t rkey() const { return rb->rkey();}
    void set_rkey(uint32_t rkey) { rb->set_rkey(rkey);}

    friend class RdmaAllocator;
};

}

#endif