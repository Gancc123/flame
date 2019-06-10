#include "memzone/rdma/RdmaMem.h"
#include "memzone/rdma/rdma_mz_log.h"
#include "util/spdk_common.h"

namespace flame{
namespace memory{
namespace ib{

RdmaBuffer::RdmaBuffer(void *ptr, BuddyAllocator *a)
: m_allocator(a){
    rdma_mem_header_t *header = static_cast<rdma_mem_header_t *>(ptr);
    m_lkey = header->lkey;
    m_rkey = header->rkey;
    m_size = header->size;
    m_buffer = static_cast<char *>(ptr);
}

void *RdmaMemSrc::alloc(size_t s) {
    auto fct = allocator_ctx->get_fct();
    void *m = spdk_malloc(s, 4096, NULL,
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

int RdmaBufferAllocator::expand(){
    auto mem_src = new RdmaMemSrc(this);
    if(!mem_src){
        return -1;
    }
    auto m = new Mutex(MUTEX_TYPE_ADAPTIVE_NP);
    if(!m){
        delete mem_src;
        return -1;
    }
    auto allocator = BuddyAllocator::create(fct, max_level, min_level, mem_src);
    if(!allocator){
        delete mem_src;
        delete m;
        return -1;
    }

    allocator->extra_data = m;

    lfl_allocators.push_back(allocator);

    return 0;
}

int RdmaBufferAllocator::init(){
    min_level = mem_cfg->rdma_mem_min_level;
    max_level = mem_cfg->rdma_mem_max_level;
    return expand(); // first expand;
}

// assume that all threads has stopped.
int RdmaBufferAllocator::fin(){
    lfl_allocators.clear();
    return 0;
}

RdmaBuffer *RdmaBufferAllocator::alloc(size_t s){
    if(s > (1ULL << max_level)){ // too large
        return nullptr;
    }
    void *p = nullptr;
    BuddyAllocator *ap = nullptr;
    int retry_cnt = 3;

retry:
    auto it = lfl_allocators.elem_iter();
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
        return nullptr; // no men can alloc
    }

    auto rb = new RdmaBuffer(p, ap);
    if(!rb){
        MutexLocker ml(mutex_of_allocator(ap));
        ap->free(p);
        return nullptr;
    }

    return rb;
}

void RdmaBufferAllocator::free(RdmaBuffer *buf){
    if(!buf) return;
    if(buf->allocator()){
        MutexLocker ml(mutex_of_allocator(buf->allocator()));
        buf->allocator()->free(buf->buffer(), buf->size());
    }
    delete buf;
}

int RdmaBufferAllocator::alloc_buffers(size_t s, int cnt, 
                                                std::vector<RdmaBuffer*> &b){
    if(s > (1ULL << max_level)){ // too large
        return 0;
    }
    int i = 0, i_before_expand = -1;
    int retry_cnt = 3;

retry:
    auto it = lfl_allocators.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);

        MutexLocker ml(mutex_of_allocator(a));
        while(i < cnt){
            void *p = a->alloc(s);
            if(p){
                auto rb = new RdmaBuffer(p, a);
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
        if(buf->allocator()){
            MutexLocker ml(mutex_of_allocator(buf->allocator()));
            buf->allocator()->free(buf->buffer(), buf->size());
        }
    }
    for(auto buf : b){
        delete buf;
    }
    b.clear();
}

size_t RdmaBufferAllocator::get_mem_used() const{
    size_t total = 0;
    auto it = lfl_allocators.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);
        total += a->get_mem_used();
        it = it->next;
    }
    return total;
}

size_t RdmaBufferAllocator::get_mem_reged() const{
    size_t total = 0;
    auto it = lfl_allocators.elem_iter();
    while(it){
        auto a = reinterpret_cast<BuddyAllocator *>(it->p);
        total += a->get_mem_total();
        it = it->next;
    }
    return total;
}

int RdmaBufferAllocator::get_mr_num() const{
    int total = 0;
    auto it = lfl_allocators.elem_iter();
    while(it){
        ++total;
        it = it->next;
    }
    return total;
}


} //namespace ib
} //namespace memory
} //namespace flame