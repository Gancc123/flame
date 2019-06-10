#include "gtest/gtest.h"
#include "common/context.h"
#include "common/thread/thread.h"
#include "common/thread/mutex.h"
#include "common/thread/cond.h"
#include "msg/msg_core.h"
#include "util/clog.h"

#include <unistd.h>
#include <cstdio>
#include <vector>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#define MSTRM std::cerr << "[          ] " 

// 2048KB
#define RDMA_MEM_MAX_LEVEL 21

namespace flame {
namespace msg{

class RdmaBufferTest : public testing::Test{
    using RdmaBuffer = ib::RdmaBuffer;
protected:
    static void SetUpTestCase(){
        auto fct = FlameContext::get_context();
        fct->init_config("flame_mgr.cfg");
        fct->init_log("", "TRACE", "client");

        mct = new MsgContext(fct);

        if(mct->load_config()){
            return ;
        }

        //config BuddyAllocator
        mct->config->rdma_mem_min_level = 10; // 1KB
        mct->config->rdma_mem_max_level = RDMA_MEM_MAX_LEVEL; 

        if(mct->init(nullptr)){
            return ;
        }

        allocator = Stack::get_rdma_stack()->get_rdma_allocator();

    }

    static void TearDownTestCase(){
        mct->fin();
        delete mct;
    }

    static ib::RdmaBufferAllocator *allocator;
    static MsgContext *mct;

    static pthread_barrier_t barrier;


    class AfThread : public Thread{
        RdmaBufferTest *owner;
        size_t cnt;
        size_t buf_size;
    public:
        explicit AfThread(RdmaBufferTest *o, size_t c, size_t bs)
        : owner(o), cnt(c), buf_size(bs) {}
        ~AfThread() {}
    protected:
        virtual void entry() override {
            srandom(time(nullptr));
            int i = cnt;
            std::vector<RdmaBuffer *> bufs;
            bufs.reserve(cnt);
            while(i){
                auto st = (random() % 5 + 1) * 100;
                usleep(st);

                auto buf = owner->allocator->alloc(buf_size);
                EXPECT_NE(buf, nullptr);
                bufs.push_back(buf);
                --i;
            }

            pthread_barrier_wait(&owner->barrier);

            for(auto buf : bufs){
                owner->allocator->free(buf);
            }

        }
    };

};

ib::RdmaBufferAllocator *RdmaBufferTest::allocator = nullptr;
MsgContext *RdmaBufferTest::mct = nullptr;
pthread_barrier_t RdmaBufferTest::barrier;

TEST_F(RdmaBufferTest, alloc_and_free){
    auto buf = allocator->alloc(4096);
    EXPECT_NE(buf, nullptr);

    auto buf2 = allocator->alloc(4096);
    EXPECT_NE(buf, nullptr);

    allocator->free(buf);

    allocator->free(buf2);
}

TEST_F(RdmaBufferTest, multi_thr_alloc_and_free){
    SCOPED_TRACE("multi_thr_alloc_and_free");
    int thr_cnt = 4;
    std::vector<AfThread *> ats;
    ats.reserve(thr_cnt);

    pthread_barrier_init(&barrier, nullptr, thr_cnt);

    // all thread will use one BuddyAllocator's max_size.
    // but may cause more BuddyAllocator expanded due to 
    // many kinds of alloc_size;
    for(int i = 0;i < thr_cnt;++i){
        size_t buf_size = 1 << (10 + i);
        size_t cnt = (1 << RDMA_MEM_MAX_LEVEL) / thr_cnt / buf_size;
        ats[i] = new AfThread(this, cnt, buf_size);
        ats[i]->create("");
    }

    for(int i = 0;i < thr_cnt;++i){
        ats[i]->join(nullptr);
        delete ats[i];
    }

    EXPECT_EQ(allocator->get_mem_used(), 0);
    MSTRM << "mr num: " << allocator->get_mr_num() << std::endl;

}

TEST_F(RdmaBufferTest, multi_thr_expand){
    SCOPED_TRACE("multi_thr_expand");
    int thr_cnt = 4;
    std::vector<AfThread *> ats;
    ats.reserve(thr_cnt);

    pthread_barrier_init(&barrier, nullptr, thr_cnt);

    // use many BuddyAllocator(4 or more)
    for(int i = 0;i < thr_cnt;++i){
        size_t buf_size = 1 << (10 + i);
        size_t cnt = (1 << RDMA_MEM_MAX_LEVEL) / buf_size; 
        ats[i] = new AfThread(this, cnt, buf_size);
        ats[i]->create("");
    }

    for(int i = 0;i < thr_cnt;++i){
        ats[i]->join(nullptr);
        delete ats[i];
    }

    EXPECT_EQ(allocator->get_mem_used(), 0);
    MSTRM << "mr num: " << allocator->get_mr_num() << std::endl;
}

} //namespace msg
} //namespace flame