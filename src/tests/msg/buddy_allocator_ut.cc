/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:17:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-10-24 17:21:02
 */
#include "gtest/gtest.h"
#include "memzone/rdma/BuddyAllocator.h"

#include <vector>
#include <cstdlib>
#include <ctime>

namespace flame {
namespace memory{
namespace ib{

struct mem_stat{
    size_t alloc_len;
};

class MemSrcDefault : public MemSrc{
public:
    virtual void *alloc(size_t s){
        return new char[s];
    }
    virtual void free(void *p){
        delete [] reinterpret_cast<char *>(p);
    }
    virtual void *prep_mem_before_return(void *p, void *base, size_t size){
        mem_stat *s = reinterpret_cast<mem_stat *>(p);
        s->alloc_len = size;
        return p;
    }
};

class BuddyAllocatorTest : public testing::Test{
protected:
    virtual void SetUp(){
        allocator = BuddyAllocator::create(nullptr, 12, 4, &mem_src);
        assert(allocator != nullptr);
        srand(time(NULL));
    }

    virtual void TearDown() {
        delete allocator;
    }

    static int rand_pick(int size){
        return rand() % size;
    }

    MemSrcDefault mem_src;
    BuddyAllocator *allocator = nullptr;
};

TEST_F(BuddyAllocatorTest, alloc_and_free){
    auto s = reinterpret_cast<mem_stat *>(allocator->alloc(15));
    ASSERT_NE(nullptr, s);
    void *tmp = s;
    EXPECT_EQ(16, s->alloc_len);
    allocator->free(s);

    s = reinterpret_cast<mem_stat *>(allocator->alloc(1023));
    ASSERT_NE(nullptr, s);
    EXPECT_EQ(1024, s->alloc_len);
    EXPECT_EQ(tmp, (void *)s);
    allocator->free(s);
}

TEST_F(BuddyAllocatorTest, no_mem_alloc){
    auto s = allocator->alloc(4096);
    ASSERT_NE(s, nullptr);

    auto s2 = allocator->alloc(16);
    ASSERT_EQ(s2, nullptr);

    auto s3 = allocator->alloc(4096);
    ASSERT_EQ(s3, nullptr);
}

TEST_F(BuddyAllocatorTest, pressure){
    std::vector<void *> p_vector;
    p_vector.resize(1 << 8, nullptr);

    for(int i = 0;i < (1 << 8);++i){
        p_vector[i] = allocator->alloc(16);
        ASSERT_NE(p_vector[i], nullptr);
    }

    for(int i = 1;i < (1 << 8);i+=2){
        allocator->free(p_vector[i]);
    }

    auto s = allocator->alloc(32);
    EXPECT_EQ(s, nullptr);

    auto s2 = allocator->alloc(32);
    EXPECT_EQ(s2, nullptr);

    auto s3 = allocator->alloc(32);
    EXPECT_EQ(s3, nullptr);

    for(int i = 0;i < (1 << 8);i+=2){
        allocator->free(p_vector[i]);
    }

    auto s4 = allocator->alloc(4096);
    EXPECT_NE(s4, nullptr);

    allocator->free(s4);
}

TEST_F(BuddyAllocatorTest, pressure2){
    std::vector<void *> p_vector;
    p_vector.resize(1 << 8, nullptr);

    for(int i = 0;i < (1 << 8);++i){
        p_vector[i] = allocator->alloc(16);
        ASSERT_NE(p_vector[i], nullptr);
    }

    while(!p_vector.empty()){
        int pick = rand_pick(p_vector.size());
        std::swap(p_vector[pick], p_vector.back());
        allocator->free(p_vector.back());
        p_vector.pop_back();
    }

    auto s4 = allocator->alloc(4096);
    EXPECT_NE(s4, nullptr);

    allocator->free(s4);

}

TEST_F(BuddyAllocatorTest, pressure3){
    std::vector<void *> p_vector;
    p_vector.resize(32, nullptr);

    for(int i = 0;i < 64; ++i){
        int odd = rand() % 2;
        uint64_t size = odd ? 1048576 : 4096;
        p_vector[i] = allocator->alloc(size);
        ASSERT_NE(p_vector[i], nullptr);
    }

    while(!p_vector.empty()){
        allocator->free(p_vector.back());
        p_vector.pop_back();
    }

    auto s4 = allocator->alloc(4096);
    EXPECT_NE(s4, nullptr);

    allocator->free(s4);

}

TEST_F(BuddyAllocatorTest, stat){
    std::vector<void *> p_vector;
    p_vector.resize(1 << 8, nullptr);

    for(int i = 0;i < (1 << 8);++i){
        p_vector[i] = allocator->alloc(16);
        ASSERT_NE(p_vector[i], nullptr);
    }

    for(int i = 1;i < (1 << 8);i+=2){
        allocator->free(p_vector[i]);
    }

    RecordProperty("Stat", allocator->get_stat());

    for(int i = 0;i < (1 << 8);i+=2){
        allocator->free(p_vector[i]);
    }

}

} //namespace ib
} //namespace msg
} //namespace flame