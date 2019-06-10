#include "memzone/rdma_mz.h"
#include "memzone/rdma/RdmaMem.h"
#include "msg/rdma/Infiniband.h"

using namespace flame::memory;
using namespace flame::memory::ib;

namespace flame {

void RdmaAllocator::init(FlameContext *fct, flame::msg::ib::ProtectionDomain *p, MemoryConfig *_cfg) {
    allocator_ctx = new flame::memory::ib::RdmaBufferAllocator(fct, _cfg, p);
    assert(allocator_ctx);
    int r = allocator_ctx->init();
    assert(r == 0);
}

RdmaAllocator::~RdmaAllocator(){
    if(allocator_ctx){
        allocator_ctx->fin();
        delete allocator_ctx;
        allocator_ctx = nullptr;
    }
}

Buffer RdmaAllocator::allocate(size_t sz) {
    RdmaBuffer *rb = nullptr;
    if(allocator_ctx){
        rb = allocator_ctx->alloc(sz);
    }
    return rb == nullptr ? Buffer() : 
                        Buffer(std::shared_ptr<BufferPtr>(new RdmaBufferPtr(rb, this)));
}

size_t RdmaAllocator::max_size() const {
    return 0;
}

size_t RdmaAllocator::min_size() const {
    return 0;
}

size_t RdmaAllocator::free_size() const {
    return 0;
}

bool RdmaAllocator::empty() const {
    return false;
}

}   //end namespace flame;
