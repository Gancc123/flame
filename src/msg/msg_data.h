/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-05 11:34:57
 */
/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief:  消息模块内部数据定义
 * @version: 0.1
 * @date: 2019-05-16
 * @copyright: Copyright (c) 2019
 * 
 * - msg_declare_id_d 用于建立连接时交换身份信息
 */
#ifndef FLAME_MSG_MSG_DATA_H
#define FLAME_MSG_MSG_DATA_H

#include "msg/msg_context.h"
#include "internal/int_types.h"
#include "internal/types.h"
#include "internal/byteorder.h"
#include "internal/msg_buffer_list.h"
#include "msg_types.h"

#include <vector>

#ifdef HAVE_RDMA
    #include "memzone/rdma/RdmaMem.h"
#endif

#define M_ENCODE(bl, data) (bl).append(&(data), sizeof(data))
#define M_DECODE(it, data) (it).copy(&(data), sizeof(data))

namespace flame{
namespace msg{

//* 这里没有考虑大小端问题，如果要移植到大端机，此处需修改。
struct msg_declare_id_d : public MsgData{
    msger_id_t msger_id;
    bool has_tcp_lp = false;
    bool has_rdma_lp = false;
    msg_node_addr_t tcp_listen_addr;
    msg_node_addr_t rdma_listen_addr;

    virtual size_t size() override {
        return sizeof(msger_id_t) 
                + sizeof(char)
                + sizeof(msg_node_addr_t) * 2;
    }

    virtual int encode(MsgBufferList& bl) override{
        int write_len = 0;
        write_len += M_ENCODE(bl, msger_id);
        char flags = 0;
        if(has_tcp_lp) flags |= 1;
        if(has_rdma_lp) flags |= 2;
        write_len += M_ENCODE(bl, flags);
        if(has_tcp_lp){
            write_len += M_ENCODE(bl, tcp_listen_addr);
        }
        if(has_rdma_lp){
            write_len += M_ENCODE(bl, rdma_listen_addr);
        }
        return write_len;
    }

    virtual int decode(MsgBufferList::iterator& it) override{
        int read_len = 0;
        read_len += M_DECODE(it, msger_id);
        char flags;
        read_len += M_DECODE(it, flags);
        if(flags & 1) has_tcp_lp = true;
        if(flags & 2) has_rdma_lp = true;
        if(has_tcp_lp){
            read_len += M_DECODE(it, tcp_listen_addr);
        }
        if(has_rdma_lp){
            read_len += M_DECODE(it, rdma_listen_addr);
        }
        return read_len;
    }
};


} //namespace msg
} //namespace flame

#undef M_ENCODE
#undef M_DECODE

#endif 