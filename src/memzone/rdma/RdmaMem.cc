/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-11-18 16:11:20
 */
#include "memzone/rdma/RdmaMem.h"
#include "common/context.h"
#include "memzone/rdma/rdma_mz_log.h"
#include "util/spdk_common.h"

namespace flame{
namespace memory{
namespace ib{

RdmaBufferAllocator* RdmaBufferAllocator::g_rdma_buffer_allocator = nullptr;

RdmaBuffer::RdmaBuffer(void *ptr, BuddyAllocator *a, RdmaBufferAllocator *rdma_buffer_allocator)
    : buddy_allocator_(a), rdma_buffer_allocator_(rdma_buffer_allocator){
    rdma_mem_header_t *header = static_cast<rdma_mem_header_t *>(ptr);
    addr_ = (uint64_t)ptr;
    size_ = header->size;
    lkey_ = header->lkey;
    rkey_ = header->rkey;
    buffer_type_ = BufferTypes::BUFF_TYPE_RDMA;
}

RdmaBuffer::~RdmaBuffer(){
    if(rdma_buffer_allocator_){
        rdma_buffer_allocator_->free(this);
    }
}


int RdmaBufferAllocator::expand(){
    auto mem_src = new RdmaMemSrc(this);
    if(!mem_src){
        return -1;
    }
    auto m = new Mutex(MUTEX_TYPE_DEFAULT);
    if(!m){
        delete mem_src;
        return -1;
    }
    FlameContext* flame_context = FlameContext::get_context();
    auto allocator = BuddyAllocator::create(flame_context, max_level_, min_level_, mem_src);
    if(!allocator){
        delete mem_src;
        delete m;
        return -1;
    }

    allocator->extra_data = m;
    lfl_allocators_.push_back(allocator);

    return 0;
}

int RdmaBufferAllocator::init(){
    min_level_ = mem_cfg_->rdma_mem_min_level;
    max_level_ = mem_cfg_->rdma_mem_max_level;
    return expand(); // first expand;
}

// assume that all threads has stopped.
int RdmaBufferAllocator::fin(){
    lfl_allocators_.clear();
    return 0;
}

Buffer RdmaBufferAllocator::allocate(size_t sz) {
    return Buffer(); //弃用
}

Buffer* RdmaBufferAllocator::allocate_ptr(size_t sz) {
    return alloc(sz);
}

int RdmaBufferAllocator::type() const{
    return BufferTypes::BUFF_TYPE_RDMA;
}

size_t RdmaBufferAllocator::max_size() const {
    return 0;
}

size_t RdmaBufferAllocator::min_size() const {
    return 0;
}

RdmaBuffer *RdmaBufferAllocator::alloc(size_t s){
    FlameContext* flame_context = FlameContext::get_context();
    if(s > (1ULL << max_level_)){ // too large
        return nullptr;
    }
    void *p = nullptr;
    BuddyAllocator *ap = nullptr;
    int retry_cnt = 3;

retry:
    auto it = lfl_allocators_.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);
        MutexLocker ml(mutex_of_allocator(a));
        p = a->alloc(s);
        if(p){
            ap = a;
            break;
        }
        it = it->next;
    }

    if(!p && retry_cnt > 0){
        retry_cnt--;
        if(expand() == 0){  // expand() success.
            goto retry;  // try again.
        }
    }

    if(!p){
        flame_context->log()->ldebug("no men can alloc");
        return nullptr; // no men can alloc
    }

    auto rb = new RdmaBuffer(p, ap, this);
    if(!rb){
        MutexLocker ml(mutex_of_allocator(ap));
        ap->free(p);
        flame_context->log()->ldebug("rb alloc error");
        return nullptr;
    }

    return rb;
}

void RdmaBufferAllocator::free(RdmaBuffer *buf){
    if(!buf) {
        assert(0); //free error
        return;
    }
    if(buf->get_buddy_allocator()){
        BuddyAllocator* buddy_allocator = buf->get_buddy_allocator();
        MutexLocker mutex_locker(mutex_of_allocator(buddy_allocator));
        buf->get_buddy_allocator()->free(buf->buffer(), buf->size());
    }
}

int RdmaBufferAllocator::alloc_buffers(size_t s, int cnt, std::vector<RdmaBuffer*> &b){
    if(s > (1ULL << max_level_)){ // too large
        return 0;
    }
    int i = 0, i_before_expand = -1;
    int retry_cnt = 3;

retry:
    auto it = lfl_allocators_.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);

        MutexLocker ml(mutex_of_allocator(a));
        while(i < cnt){
            void *p = a->alloc(s);
            if(p){
                auto rb = new RdmaBuffer(p, a, this);
                if(!rb){
                    //RdmaBuffer new failed???
                    a->free(p);
                    break;
                }
                b.push_back(rb);
                ++i;
            }else{
                break;
            }
        }
        if(i == cnt){
            break;
        }

        it = it->next;
    }

    // alloc failed after expand()?
    if(i < cnt && (i != i_before_expand || retry_cnt > 0 )){ 
        i_before_expand = i;
        retry_cnt--;
        if(expand() == 0){  // expand() success.
            goto retry;  // try again.
        }
    }
    
    return i;
}

void RdmaBufferAllocator::free_buffers(std::vector<RdmaBuffer*> &b){
    for(auto buf : b){
        if(!buf) continue;
        if(buf->get_buddy_allocator()){
            MutexLocker ml(mutex_of_allocator(buf->get_buddy_allocator()));
            buf->get_buddy_allocator()->free(buf->buffer(), buf->size());
        }
    }
    for(auto buf : b){
        delete buf;
    }
    b.clear();
}

size_t RdmaBufferAllocator::get_mem_used() const{
    size_t total = 0;
    auto it = lfl_allocators_.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);
        total += a->get_mem_used();
        it = it->next;
    }
    return total;
}

size_t RdmaBufferAllocator::get_mem_reged() const{
    size_t total = 0;
    auto it = lfl_allocators_.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);
        total += a->get_mem_total();
        it = it->next;
    }
    return total;
}

int RdmaBufferAllocator::get_mr_num() const{
    int total = 0;
    auto it = lfl_allocators_.elem_iter();
    while(it){
        ++total;
        it = it->next;
    }
    return total;
}

void *RdmaMemSrc::alloc(size_t s) {
    FlameContext* fct = FlameContext::get_context();
    void *m = spdk_malloc(s, 1 << 21, NULL,
                            SPDK_ENV_SOCKET_ID_ANY,
                            SPDK_MALLOC_DMA | SPDK_MALLOC_SHARE);

    if(!m){
        FL(fct, error, "RdmaMemSrc failed to allocate {} B of mem.", s);
        return nullptr;
    }
    // When huge page memory's unit is 2MB(on intel),
    // even s < 2MB, m will be at least 2MB.
    // But when reg m(2MB) with s(s < 2MB), ibv_reg_mr give an error????
    mr = ibv_reg_mr(allocator_ctx->get_pd()->pd, m, s, 
                        IBV_ACCESS_LOCAL_WRITE
                        | IBV_ACCESS_REMOTE_WRITE
                        | IBV_ACCESS_REMOTE_READ);

    if(mr == nullptr){
        FL(fct, error, "RdmaMemSrc failed to register {} B of mem: {}", 
                    s, strerror(errno));
        spdk_free(m);
        return nullptr;
    }else{
        FL(fct, info, "RdmaMemSrc register {} B of mem", s);
    }

    return m;
}

void RdmaMemSrc::free(void *p) {
    ibv_dereg_mr(mr);
    spdk_free(p);
}

void *RdmaMemSrc::prep_mem_before_return(void *p, void *base, size_t size) {
    rdma_mem_header_t *header = reinterpret_cast<rdma_mem_header_t *>(p);
    header->lkey = mr->lkey;
    header->rkey = mr->rkey;
    header->size = size;
    return p;
}


} //namespace ib
} //namespace memory
} //namespace flame