/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-21 09:36:46
 */
/**
 * @file buffer.h
 * @author zhzane (zhzane@outlook.com)
 * @brief 内存分配接口
 * @version 0.1
 * @date 2019-05-14
 * 
 * @copyright Copyright (c) 2019
 * 
 * - BufferPtr: 内存区域指针接口，由具体实现继承，需要在析构函数中处理内存回收
 * - Buffer: 共享指针的封装，指向 BufferPtr
 * - BufferList: Buffer列表的封装实现
 * - BufferAllocator: Buffer分配器接口，所有内存分配器都需要继承该接口
 * 
 */
#ifndef FLAME_INCLUDE_BUFFER_H
#define FLAME_INCLUDE_BUFFER_H

#include "common/context.h"

#include <cassert>
#include <cstdint>
#include <list>
#include <memory>
#include <iostream>

enum BufferTypes {
    BUFF_TYPE_NORMAL    = 0,
    BUFF_TYPE_DMA       = 0x1,
    BUFF_TYPE_RDMA      = 0x2
};

namespace flame {

class BufferPtr {
public:
    virtual ~BufferPtr() {}

    /**
     * @brief Get Address of Buffer
     * 
     * @return void* 
     */
    virtual void* addr() const = 0;

    /**
     * @brief Get Size of Buffer
     * 
     * @return size_t 
     */
    virtual size_t size() const = 0;

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    virtual bool null() const { return addr() == nullptr || size() == 0; }

    /**
     * @brief Get the Tail of Buffer
     * 
     * @return void* 
     */
    virtual void* end() const { return (char *)addr() + size(); }

    /**
     * @brief Buffer类型
     * 用于区分不同Buffer分配器所分配的类型，0表示无类型
     * @return int 
     */
    virtual int type() const { return 0; }

    /**
     * @brief rkey of Buffer
     * 
     * @return uint32_t 
     */
    virtual uint32_t rkey() const = 0;

    /**
     * @brief Get lkey of Buffer
     * 
     * @return uint32_t 
     */
    virtual uint32_t lkey() const = 0;

    /**
     * @brief 按指定大小调整Buffer空间
     * 默认拒绝重新分配，可以不实现调整功能
     * @param sz 新大小
     * @return true 调整成功
     * @return false 调整失败
     */
    virtual bool resize(size_t sz) { return size() == sz ? true : false; }
 
protected:
    BufferPtr() {}
}; // class BufferPtr

inline bool operator == (const BufferPtr& x, const BufferPtr& y) {
    return (x.addr() == y.addr()) && (x.size() == y.size());
}

inline bool operator != (const BufferPtr& x, const BufferPtr& y) {
    return !(x == y);
}

template<typename T>
class Pointer : public BufferPtr {
public:
    explicit Pointer() : ptr_(nullptr), cnt_(0) {}
    explicit Pointer(T* ptr) : ptr_(ptr), cnt_(1) {}
    explicit Pointer(T* ptr, size_t cnt) : ptr_(ptr), cnt_(cnt) {}

    virtual uint8_t* addr() const override { return static_cast<uint8_t*>(ptr_); }

    virtual size_t size() const override { return sizeof(T) * cnt_; }

    void set(T* ptr, size_t cnt = 1) { ptr_ = ptr; cnt_ = cnt; }

    T* get(size_t idx = 0) { return idx < cnt_ ? ptr_ + idx : nullptr; }

protected:
    T* ptr_;
    size_t cnt_;
}; // class Pointer

class Buffer {
public:
    Buffer() {}
    ~Buffer() {
        FlameContext* fct = FlameContext::get_context();
        fct->log()->ldebug("Buffer", "Buffer deleted!");
    }
    Buffer(const std::shared_ptr<BufferPtr>& ptr) : ptr_(ptr) {}

    /**
     * @brief 
     * 
     * @param ptr 
     */
    inline void set(const std::shared_ptr<BufferPtr>& ptr) { ptr_ = ptr; }

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    inline bool null() const { return ptr_.get() == nullptr || ptr_->null(); }

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    inline bool valid() const { return ! null(); }

    /**
     * @brief 
     * 
     * @return void* 
     */
    inline void* addr() const { assert(ptr_ != nullptr); return ptr_->addr(); }

    /**
     * @brief 
     * 
     * @return size_t 
     */
    inline size_t size() const { return ptr_->size(); }

    /**
     * @brief  
     * 
     * @return uint8_t* 
     */
    inline void* end() const { return ptr_->end(); }

    /**
     * @brief 
     * 
     * @return int 
     */
    inline int type() const { return ptr_->type(); }

    /**
     * @brief 
     * 
     * @return rkey 
     */
    inline uint32_t rkey() const { return ptr_->rkey(); }

    /**
     * @brief 
     * 
     * @return lkey 
     */
    inline uint32_t lkey() const { return ptr_->lkey(); }

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    inline bool is_normal() const { return ptr_->type() == BUFF_TYPE_NORMAL; }
    inline bool is_dma() const { return ptr_->type() == BUFF_TYPE_DMA; }
    inline bool is_rdma() const { return ptr_->type() == BUFF_TYPE_RDMA; }

    /**
     * @brief 
     * 
     * @param sz 
     * @return true 
     * @return false 
     */
    inline bool resize(size_t sz) { return ptr_->resize(sz); }

    /**
     * @brief 
     * 
     * @return BufferPtr* 
     */
    inline BufferPtr* get() const { return ptr_.get(); }

    /**
     * @brief 清楚引用
     * 
     */
    inline void clear() { ptr_.reset(); }

    Buffer(const Buffer&) = default;
    Buffer(Buffer&&) = default;
    Buffer& operator = (const Buffer&) = default;
    Buffer& operator = (Buffer&&) = default;

private:
    std::shared_ptr<BufferPtr> ptr_ {nullptr};
}; // class Buffer

class BufferList {
public:
    BufferList() {}
    explicit BufferList(const Buffer& buff) { }

    /**
     * @brief Buffer总大小
     * 
     * @return size_t 
     */
    inline size_t size() const { return bsize_; }

    /**
     * @brief Buffer分段数量
     * 
     * @return size_t 
     */
    inline size_t count() const { return blist_.size(); }

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    inline bool empty() const { return count() == 0 || size() == 0; }

    /**
     * @brief 头部迭代器
     * 
     * @return std::list<Buffer>::const_iterator 
     */
    inline std::list<Buffer>::const_iterator begin() const { return blist_.cbegin(); }

    /**
     * @brief 尾部迭代器
     * 
     * @return std::list<Buffer>::const_iterator 
     */
    inline std::list<Buffer>::const_iterator end() const { return blist_.cend(); }
    
    /**
     * @brief 反向-头部迭代器
     * 
     * @return std::list<Buffer>::const_reverse_iterator 
     */
    inline std::list<Buffer>::const_reverse_iterator rbegin() const { return blist_.crbegin(); }
    
    /**
     * @brief 反向-尾部迭代器
     * 
     * @return std::list<Buffer>::const_reverse_iterator 
     */
    inline std::list<Buffer>::const_reverse_iterator rend() const { return blist_.crend(); }

    /**
     * @brief 追加到头部
     * 
     * @param buff 
     */
    inline void push_front(const Buffer& buff) {
        blist_.push_front(buff);
        bsize_ += buff.size();
    }

    /**
     * @brief 追加到头部
     * 
     * @param bl 
     */
    inline void push_front(const BufferList& bl) {
        if (&bl == this || bl.empty()) 
            return;
        for (auto it = bl.rbegin(); it != bl.rend(); it++)
            blist_.push_front(*it);
        bsize_ += bl.size();
    }

    /**
     * @brief 弹出头部
     * 
     */
    inline void pop_front() {
        if (blist_.empty())
            return;
        bsize_ -= blist_.front().size();
        blist_.pop_front();
    }

    /**
     * @brief 追加到尾部
     * 
     * @param buff 
     */
    inline void push_back(const Buffer& buff) {
        blist_.push_back(buff);
        bsize_ += buff.size();
    }

    /**
     * @brief 追加到尾部
     * 
     * @param bl 
     */
    inline void push_back(const BufferList& bl) {
        if (&bl == this || bl.empty())
            return;
        for (auto it = bl.begin(); it != bl.end(); it++)
            blist_.push_back(*it);
        bsize_ += bl.size();
    }

    /**
     * @brief 弹出尾部
     * 
     */
    inline void pop_back() {
        if (blist_.empty())
            return;
        bsize_ -= blist_.back().size();
        blist_.pop_back();
    }

    BufferList(const BufferList&) = default;
    BufferList(BufferList&&) = default;
    BufferList& operator = (const BufferList&) = default;
    BufferList& operator = (BufferList&&) = default;

private:
    std::list<Buffer> blist_;
    size_t bsize_ {0};
}; // class BufferList

/**
 * @brief Buffer分配器
 * 
 * 所有具体实现都需要定义其对应的BufferAllocator和BufferPtr,
 * Buffer分配器没有定义回收的方法，回收动作在BufferPtr指针被Buffer释放时触发
 * 
 */
class BufferAllocator {
public:
    virtual ~BufferAllocator() {}

    /**
     * @brief 获得内存分配器
     * 
     * @return BufferAllocator*
     */
    // virtual BufferAllocator* get_buffer_allocator() const = 0;
    /**
     * @brief 所分配的内存类型
     * 
     * @return int 
     */
    virtual int type() const = 0;

    /**
     * @brief 
     * 
     * @return true 
     * @return false 
     */
    inline bool is_normal() const { return this->type() == BUFF_TYPE_NORMAL; }
    inline bool is_dma() const { return this->type() == BUFF_TYPE_DMA; }
    inline bool is_rdma() const { return this->type() == BUFF_TYPE_RDMA; }

    /**
     * @brief 所能分配的最大内存（不是Free空间）
     * 
     * @return size_t 
     */
    virtual size_t max_size() const = 0;

    /**
     * @brief 所能分配的最小内存
     * 
     * @return size_t 
     */
    virtual size_t min_size() const = 0;

    /**
     * @brief 剩余空间
     * 
     * @return size_t 当有剩余空间，但无法准确获取时，返回BUFF_ALLOC_FULL_SIZE
     */
    virtual size_t free_size() const = 0;
#define BUFF_ALLOC_FULL_SIZE    (~0ULL)

    /**
     * @brief 为空
     * 
     * @return true 没有剩余空间
     * @return false 
     */
    virtual bool empty() const = 0;

    /**
     * @brief 分配指定大小的Buffer
     * 需要检查Buffer是否有效(valid)，
     * 当Buffer的is_null()返回true或valid()返回false时，
     * 表示内存分配器不能够再分配指定大小的内存
     * @param sz 
     * @return Buffer 
     */
    virtual Buffer allocate(size_t sz) = 0;

    virtual Buffer* allocate_ptr(size_t sz) = 0;
protected:
    BufferAllocator() {}
}; // class BufferAllocator

} // namespace flame

#endif // !FLAME_INCLUDE_BUFFER_H
