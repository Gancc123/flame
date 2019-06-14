/**
 * @author: hzy (lzmyhzy@gmail.com)
 * @brief: 消息模块消息实体
 * @version: 0.1
 * @date: 2019-03-11
 * @copyright: Copyright (c) 2019
 * 
 * - Msg 用于向Connection发送消息，RdmaConnectionV2模式下使用较少
 */
#ifndef FLAME_MSG_MSG_H
#define FLAME_MSG_MSG_H

#include <list>
#include <vector>
#include <utility>
#include <stdexcept>
#include <mutex>
#include <iomanip>
#include <algorithm>

#include "msg_common.h"

namespace flame{
namespace msg{

class Msg : public RefCountedObject{
    MsgBufferList data_bl;

public:
    /**
     * @brief 分配消息并初始化消息部分字段
     * @param mct 消息模块上下文
     * @param ttype 消息模块传输类型(TCP, RDMA)
     * @return 消息实例
     */
    static Msg* alloc_msg(MsgContext *mct, 
                                    msg_ttype_t ttype=msg_ttype_t::TCP){
        Msg *msg = new Msg(mct);
        msg->ttype = ttype;
        msg->type = FLAME_MSG_TYPE_CTL;
        return msg;
    }

    explicit Msg(MsgContext *mct)
    : RefCountedObject(mct){
        type = FLAME_MSG_TYPE_NONE;
        priority = FLAME_MSG_PRIO_DEFAULT;
    };

    ~Msg() {};

    /**
     * @brief 解码消息头
     * 消息头部为消息最开始的数据，字节长度固定
     * @param buffer 用于读取数据的buffer
     * @return > 0 读取的字节数
     * @return -1 解码出错
     */
    ssize_t decode_header(MsgBuffer &buffer);

    /**
     * @brief 编码消息头
     * 消息头部为消息最开始的数据，字节长度固定
     * @param buffer 用于写入数据的buffer
     * @return > 0 写入的字节数
     * @return -1 编码出错
     */
    ssize_t encode_header(MsgBuffer &buffer);

    /**
     * @brief 将数据追加到Msg中
     * 存在一次内存拷贝
     * @param buf 数据来源
     * @return 追加的字节数
     */
    int append_data(MsgBuffer &buf){
        return data_bl.append(buf);
    }

    /**
     * @brief 将数据追加到Msg中
     * 存在一次内存拷贝
     * @param buf 数据来源指针
     * @param len 数据字节数
     * @return 追加的字节数
     */
    int append_data(void *buf, size_t len){
        return  data_bl.append(buf, len);
    }

    /**
     * @brief 将数据追加到Msg中
     * 存在一次内存拷贝
     * @param d 数据来源
     * @return 追加的字节数
     */
    int append_data(MsgData &d){
        return d.encode(data_bl);
    }

    /**
     * @brief 返回消息数据部分的迭代器
     * @return MsgBufferList::iterator
     */
    MsgBufferList::iterator data_iter(){
        return data_bl.begin();
    }

    /**
     * @brief 返回数据部分的MsgBufferList引用
     * @return MsgBufferList
     */
    MsgBufferList& data_buffer_list(){
        return data_bl;
    }

    /**
     * @brief 返回消息的数据部分的字节数
     * 使用该函数，而不是data_len字段。data_len字段仅在接收数据时使用
     * @return 数据部分的字节数
     */
    size_t get_data_len() const{
        return data_bl.length();
    }

    /**
     * @brief 消息的总字节数
     * 消息头字节数 + 消息数据部分字节数
     * @return 总字节数
     */
    size_t total_bytes() const{
        return get_data_len() + sizeof(flame_msg_header_t);
    }

    /**
     * @brief 清空数据部分
     */
    void clear_data(){
        data_bl.clear();
    }

    /**
     * @brief 当前数据的buffer
     * @param cd_len 当前数据buffer的字节数
     * @return 返回当前数据的buffer指针
     */
    char *cur_data_buffer(int &cd_len){
        int cd_len_bl = 0;
        char *buffer = data_bl.cur_data_buffer(cd_len_bl, 
                                                data_len - data_bl.length());
        cd_len = std::min((int)(data_len - data_bl.length()), cd_len_bl);
        return buffer;
    }

    /**
     * @brief 向当前数据buffer中追加字节数
     * 在拷贝数据后，调整buffer内部的偏移指针
     * @param ex_len 追加的字节数目
     * @return 成功追加的字节数
     */
    int cur_data_buffer_extend(int ex_len){
        if(ex_len > data_len - data_bl.length()){
            ex_len = data_len - data_bl.length();
        }
        int r =  data_bl.cur_data_buffer_extend(ex_len);
        return r;
    }

    /**
     * @brief 消息类型是否为控制信息
     */
    bool is_ctl() const {
        return (this->type == FLAME_MSG_TYPE_CTL);
    }

    /**
     * @brief 消息类型是否为io数据
     */
    bool is_io() const {
        return (this->type == FLAME_MSG_TYPE_IO);
    }

    /**
     * @brief 消息类型是否为声明消息
     */
    bool is_declare_id() const{
        return (this->type == FLAME_MSG_TYPE_DECLARE_ID);
    }

    /**
     * @brief 消息类型是否为立即数
     */
    bool is_imm_data() const{
        return (this->type == FLAME_MSG_TYPE_IMM_DATA);
    }

    /**
     * @brief 消息类型是否为无操作
     */
    bool is_nop() const {
        return (this->type == FLAME_MSG_TYPE_NONE);
    }

    /**
     * @brief 设置消息标志位
     * @param flags 标志位
     * @param remove 是否置0
     */
    void set_flags(uint8_t flags, bool remove=false) {
        if(!remove){
            this->flag |= flags;
        }else{
            this->flag &= (~flags);
        }
    }

    /**
     * @brief 消息是否为请求
     */
    bool is_req() const{
        return !is_resp();
    }

    /**
     * @brief 消息是否为响应
     */
    bool is_resp() const {
        return (this->flag & FLAME_MSG_FLAG_RESP);
    }

    /**
     * @brief 消息是否包含立即数
     */
    bool with_imm() const {
        return (this->flag & FLAME_MSG_FLAG_WITH_IMM);
    }

    /**
     * @brief 消息是否包含RDMA头部
     * 是否需要进行自动的RDMA传输(原来是这么设计的，不过自动部分尚未实现)
     * 进行RDMA传输推荐使用RdmaConnectionV2接口，不建议使用Msg
     */
    bool has_rdma() const {
        return (this->flag & FLAME_MSG_FLAG_RDMA);
    }

    /**
     * @brief RDMA传输是MemPush还是MemFetch
     */
    uint8_t get_rdma_type() const{
        return FLAME_MSG_RDMA_TYPE(flag);
    }

    /**
     * @brief RDMA信息头部的个数
     */
    uint8_t get_rdma_cnt() const{
        return FLAME_MSG_RDMA_CNT(reserved);
    }

    /**
     * @brief 设置RDMA类型
     * @param t FLAME_MSG_RDMA_MEM_FETCH或FLAME_MSG_RDMA_MEM_PUSH
     */
    void set_rdma_type(uint8_t t){
        set_flags(FLAME_MSG_FLAG_MEM_FETCH, t != FLAME_MSG_RDMA_MEM_FETCH);
    }

    /**
     * @brief 设置RDMA信息头部个数
     */
    void set_rdma_cnt(uint8_t cnt){
        reserved = cnt;
    }

    /**
     * @brief 返回消息类型字符串
     */
    std::string msg_type_str() const{
        switch(this->type){
        case FLAME_MSG_TYPE_NONE:          return "TYPE_NONE";
        case FLAME_MSG_TYPE_CTL:           return "TYPE_CTL";
        case FLAME_MSG_TYPE_IO:            return "TYPE_IO";
        case FLAME_MSG_TYPE_DECLARE_ID:    return "TYPE_DECLARE_ID";
        case FLAME_MSG_TYPE_IMM_DATA:      return "TYPE_IMM_DATA";
        default:                           return "TYPE_INVALID";
        }
    }

    /**
     * @brief 返回消息标志位字符串
     */
    std::string msg_flag_str() const{
        char flags[5];
        flags[sizeof(flags) - 1] = '\0';
        flags[3] = with_imm()?'I':'-';
        flags[2] = has_rdma()?'M':'-';
        flags[1] = is_resp()?'R':'-';
        flags[0] = '-';
        return flags;
    }

    /**
     * @brief 返回log时使用的Msg字符串
     */
    std::string to_string() const{
        auto s = fmt::format("[Msg {} {} {} PRIO{} {}B]({:p})",
                                msg_ttype_to_str(ttype),
                                msg_type_str(), msg_flag_str(),
                                (int)priority, get_data_len(),
                                (void *)this);
        return s;
    }

    msg_ttype_t ttype = msg_ttype_t::TCP; //just for transport type

    uint8_t  type; // msg type
    uint8_t  flag = 0; // msg flag
    uint8_t  priority; 
    uint8_t  reserved = 0;
    uint32_t data_len = 0; // only for receive
    uint32_t imm_data = 0; // only for FLAME_MSG_TYPE_IMM_DATA

    /**
     * @brief 返回RDMA信息部分的字节数
     */
    size_t payload_len() const{
        size_t total = 0;
        if(with_imm()){
            total += sizeof(flame_msg_imm_data_t);
        }
        if(has_rdma()){
            total += get_rdma_cnt() * sizeof(flame_msg_rdma_mr_t);
        }
        return total;
    }

};

} //namespace msg
} //namespace flame

#endif //FLAME_MSG_MESSAGE_H
