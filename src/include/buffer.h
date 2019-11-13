/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-11-12 17:07:59
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

class Buffer {
public:
    Buffer() {}
    Buffer(uint64_t addr, size_t size, uint32_t lkey, uint32_t rkey, BufferTypes buffer_type)
    : addr_(addr), size_(size), lkey_(lkey), rkey_(rkey), buffer_type_(buffer_type){}

    ~Buffer() {
        FlameContext* fct = FlameContext::get_context();
        fct->log()->ldebug("Buffer", "Buffer deleted! 0x%x", this);
    }
    inline uint64_t addr() const { return addr_; }
    inline size_t size() const { return size_; }
    inline uint32_t lkey() const { return lkey_; }
    inline uint32_t rkey() const { return rkey_; }
    inline int buffer_type() const { return buffer_type_; }

    inline void set_addr(uint64_t addr)  { addr_ = addr; }
    inline void set_size(size_t   size)  { size_ = size; }
    inline void set_lkey(uint32_t lkey)  { lkey_ = lkey; }
    inline void set_rkey(uint32_t rkey)  { rkey_ = rkey; }
    inline void set_buffer_type(BufferTypes buffer_type)  { buffer_type_ = buffer_type; }

    inline bool is_normal() const { return buffer_type_ == BUFF_TYPE_NORMAL; }
    inline bool is_dma() const { return buffer_type_ == BUFF_TYPE_DMA; }
    inline bool is_rdma() const { return buffer_type_ == BUFF_TYPE_RDMA; }

    // inline bool resize(size_t sz) { return ptr_->resize(sz); }
    Buffer(const Buffer&) = default;
    Buffer(Buffer&&) = default;
    Buffer& operator = (const Buffer&) = default;
    Buffer& operator = (Buffer&&) = default;
protected:
    uint64_t addr_;
    size_t   size_;
    uint32_t lkey_;
    uint32_t rkey_;
    BufferTypes buffer_type_;
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

    virtual int type() const = 0;

    inline bool is_normal() const { return this->type() == BUFF_TYPE_NORMAL; }
    inline bool is_dma() const { return this->type() == BUFF_TYPE_DMA; }
    inline bool is_rdma() const { return this->type() == BUFF_TYPE_RDMA; }

    virtual size_t max_size() const = 0;
    virtual size_t min_size() const = 0;

    /**
     * @brief 剩余空间
     * 
     * @return size_t 当有剩余空间，但无法准确获取时，返回BUFF_ALLOC_FULL_SIZE
     */
    virtual size_t free_size() {}
#define BUFF_ALLOC_FULL_SIZE    (~0ULL)
    virtual bool empty() {}

    virtual Buffer allocate(size_t sz) = 0;
    virtual Buffer* allocate_ptr(size_t sz) = 0;
protected:
    BufferAllocator() {}
}; // class BufferAllocator

} // namespace flame

#endif // !FLAME_INCLUDE_BUFFER_H
