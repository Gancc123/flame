/**
 * @file cmd.h
 * @author zhzane (zhzane@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2019-05-06
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef FLAME_INCLUDE_CMD_H
#define FLAME_INCLUDE_CMD_H

#define CACHE_LINE_SHIFT    6
#define CACHE_LINE_SIZE     (1U << CACHE_LINE_SHIFT)

#include <stdint.h>
#include <queue>
#include <map>

#include "msg/msg_core.h"
#include "libflame/libchunk/log_libchunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Command Base 
 */

/**
 * Command Number
 * @length: 16 bit (2 Bytes)
 */
struct cmd_num_t {
    uint8_t cls;    // command class
    uint8_t seq;    // command sequence
} __attribute__((packed));

// 控制平面（预留类型）
#define CMD_CLS_MGR 0x00U   // 0x00 ~ 0x0F for MGR
#define CMD_CLS_CSD 0x10U   // 0x10 ~ 0x1F for CSD
#define CMD_CLS_TGT 0x20U   // 0x20 ~ 0x2F for TGT / GW
#define CMD_CLS_MSG 0x30U   // 0x30 ~ 0x3F for Msg module

// 数据平面
#define CMD_CLS_IO_CHK 0xF0U    // Chunk

/**
 * Command Header
 * @length: 64 bit (8 Bytes)
 */
struct cmd_hdr_t {
    uint16_t    len;    // 16 bit; 以 CACHE_LINE_SIZE 为单位
    cmd_num_t   cn;     // 16 bit;
    uint8_t     flg;    // 8 bit; Flags. 
    uint8_t     cqg;    // 8 bit; Command Queue Group. 最多支持 256 个队列
    uint16_t    cqn;    // 16 bit; Command Queue Number. 队列深度最大为 64K
} __attribute__((packed));

/**
 * Chunk ID
 * @length: 64 bit (8 Bytes)
 */
typedef uint64_t chk_id_t;

/**
 * Chunk Offset
 * @length: 32 bit (4 Bytes)
 * 以 CACHE_LINE_SIZE 为单位，因此支持最大的 Chunk 尺寸为 256 GB
 */
typedef uint32_t chk_off_t;

#define chk_off_to_real(off) ((uint64_t)off << CACHE_LINE_SHIFT)

/**
 * Command Return Code
 * @length: 
 */
typedef uint16_t cmd_rc_t;

/**
 * Empty Command
 * @length: 64 Bytes
 */
#define CMD_RESERVED_SIZE (CACHE_LINE_SIZE - sizeof(struct cmd_hdr_t))

struct cmd_t {
    cmd_hdr_t   hdr;    // 8 Bytes
    uint8_t     cont[CMD_RESERVED_SIZE];
} __attribute__ ((packed));

/**
 * Emtpy Command Response
 * 
 */
#define CMD_RES_RESERVED_SIZE (CACHE_LINE_SIZE - sizeof(struct cmd_hdr_t) - sizeof(cmd_rc_t))

struct cmd_res_t {
    cmd_hdr_t   hdr;    // 8 Bytes
    cmd_rc_t    rc;     // 2 Bytes
    uint8_t     cont[CMD_RES_RESERVED_SIZE];
} __attribute__((packed));

/**
 * @brief Command Memory Area
 * 
 */
struct cmd_ma_t {
    uint64_t addr;
    uint32_t len;
    uint32_t key;
} __attribute__ ((packed));

/**
 * Command Callback
 */
typedef void (*cmd_fn_t)(void* arg, struct cmd_t* cmd, void* res, cmd_rc_t rc);

#ifdef __cplusplus
}
#endif

#include <string>
#include <memory>

namespace flame {

class Command {
public:
    /**
     * @brief 将命令拷贝到指定内存区域
     * 
     * @param buff 
     */
    virtual void copy(void* buff) = 0;

    /**
     * @brief Get cmd_t
     * 
     * @return cmd_t 
     */
    inline cmd_t get_cmd() const { return *cmd_; }

    /**
     * @brief Get Command Number
     * 
     * @return uint16_t 
     */
    inline uint16_t get_num() const { return *(uint16_t*)&cmd_->hdr.cn; }

    /**
     * @brief Get Command Number
     * 
     * @return uint16_t 
     */
    inline cmd_num_t get_cn() const { return cmd_->hdr.cn; }

    /**
     * @brief Get Command Class
     * 
     * @return uint16_t 
     */
    inline uint8_t get_cls() const { return *(uint16_t*)&cmd_->hdr.cn.cls; }

    /**
     * @brief Get Command Sequence
     * 
     * @return uint16_t 
     */
    inline uint8_t get_seq() const { return *(uint16_t*)&cmd_->hdr.cn.seq; }

    /**
     * @brief Get Command Queue Group
     * 
     * @return uint8_t 
     */
    inline uint8_t get_cqg() const { return cmd_->hdr.cqg; }

    /**
     * @brief Set Command Queue Group
     * 
     * @param cqg 
     */
    inline void set_cqg(uint8_t cqg) { cmd_->hdr.cqg = cqg; }

    /**
     * @brief Get Command Queue Number
     * 
     * @return uint16_t 
     */
    inline uint16_t get_cqn() const { return cmd_->hdr.cqn; }

    /**
     * @brief Set Command Queue Number
     * 
     * @param cqn 
     */
    inline void set_cqn(uint16_t cqn) { cmd_->hdr.cqn = cqn; }

    /**
     * @brief Set Command Queue Infomation
     * 
     * @param cqg 
     * @param cqn 
     */
    inline void set_cq(uint8_t cqg, uint16_t cqn) {
        set_cqg(cqg);
        set_cqn(cqn);
    }

    /**
     * @brief Get Real Length of Command Structure
     * Cache Line Size Aligned
     * @return uint32_t 
     */
    inline uint32_t get_len() const { return chk_off_to_real(cmd_->hdr.len); }

    /**
     * @brief Get Content of Command
     * 
     * @return void* 
     */
    inline void* get_content() const { return cmd_->cont; }

    friend class Response;

protected:
    Command(cmd_t* cmdp) : cmd_(cmdp) {}
    virtual ~Command() {}

    /**
     * @brief Set Command Number
     * 
     * @param cls 
     * @param seq 
     */
    inline void set_num(uint8_t cls, uint8_t seq) {
        cmd_->hdr.cn.cls = cls;
        cmd_->hdr.cn.seq = seq;
    }

    /**
     * @brief Set Command Flags
     * 
     * @param flg 
     */
    inline void set_flg(uint8_t flg) { cmd_->hdr.flg = flg; }

    /**
     * @brief Set Inner Length
     * cmd_t 的 len 以 CacheLine 尺寸为单位
     * @param len 
     */
    inline void set_len_inner(uint16_t len) { cmd_->hdr.len = len; }

    /**
     * @brief Set the len real object
     * 计算成 Cache Line 对齐的方式
     * @param len 
     */
    inline void set_len(uint32_t len) {
        set_len_inner(len % CACHE_LINE_SIZE ? len / CACHE_LINE_SIZE + 1 : len / CACHE_LINE_SIZE);
    }

    /**
     * @brief Set Command Header
     * 
     * @param cls 
     * @param seq 
     * @param flg 
     * @param len 
     */
    inline void set_hdr(uint8_t cls, uint8_t seq, uint8_t flg, uint32_t len) {
        set_num(cls, seq);
        set_flg(flg);
        set_len(len);
    }

    cmd_t* cmd_;
}; // class Command

class Response {
public:
    /**
     * @brief 将命令拷贝到指定内存区域
     * 
     * @param buff 
     */
    // virtual void copy(void* buff) = 0;

    /**
     * @brief Get Command Number
     * 
     * @return uint16_t 
     */
    inline uint16_t get_num() const { return *(uint16_t*)&res_->hdr.cn; }

    /**
     * @brief Get Command Number type cmd_num_t
     * 
     * @return uint16_t 
     */
    inline cmd_num_t get_num_t() const { return res_->hdr.cn; }

    /**
     * @brief Get Command Queue Group
     * 
     * @return uint8_t 
     */
    inline uint8_t get_cqg() const { return res_->hdr.cqg; }

    /**
     * @brief Set Command Queue Group
     * 
     * @param cqg 
     */
    inline void set_cqg(uint8_t cqg) { res_->hdr.cqg = cqg; }

    /**
     * @brief Get Command Queue Number
     * 
     * @return uint16_t 
     */
    inline uint16_t get_cqn() const { return res_->hdr.cqn; }

    /**
     * @brief Set Command Queue Number
     * 
     * @param cqn 
     */
    inline void set_cqn(uint16_t cqn) { res_->hdr.cqn = cqn; }

    /**
     * @brief Set Command Queue Infomation
     * 
     * @param cqg 
     * @param cqn 
     */
    inline void set_cq(uint8_t cqg, uint16_t cqn) {
        set_cqg(cqg);
        set_cqn(cqn);
    }

    /**
     * @brief Get Real Length of Command Structure
     * Cache Line Size Aligned
     * @return uint32_t 
     */
    inline uint32_t get_len() const { return chk_off_to_real(res_->hdr.len); }

    /**
     * @brief Get Return Code
     * 
     * @return cmd_rc_t 
     */
    inline cmd_rc_t get_rc() const { return res_->rc; }

    /**
     * @brief Set Return Code
     * 
     * @param rc 
     */
    inline void set_rc(cmd_rc_t rc) { res_->rc = rc; }

    /**
     * @brief Get Content of Command
     * 
     * @return void* 
     */
    inline void* get_content() const { return res_->cont; }

    inline void cpy_hdr(const Command& command) {
        res_->hdr.cn.cls = command.get_cls();
        res_->hdr.cn.seq = command.get_seq(); 
        res_->hdr.cqg = command.get_cqg();
        res_->hdr.cqn = command.get_cqn();
        // res_->hdr.flg = command.get
    }

protected:
    Response(cmd_res_t* resp) : res_(resp) {}
    virtual ~Response() {}

        /**
     * @brief Set Command Number
     * 
     * @param cls 
     * @param seq 
     */
    inline void set_num(uint8_t cls, uint8_t seq) {
        res_->hdr.cn.cls = cls;
        res_->hdr.cn.seq = seq;
    }

    /**
     * @brief Set Command Flags
     * 
     * @param flg 
     */
    inline void set_flg(uint8_t flg) { res_->hdr.flg = flg; }

    /**
     * @brief Set Inner Length
     * cmd_t 的 len 以 CacheLine 尺寸为单位
     * @param len 
     */
    inline void set_len_inner(uint16_t len) { res_->hdr.len = len; }

    /**
     * @brief Set the len real object
     * 计算成 Cache Line 对齐的方式
     * @param len 
     */
    inline void set_len(uint32_t len) {
        set_len_inner(len % CACHE_LINE_SIZE ? len / CACHE_LINE_SIZE + 1 : len / CACHE_LINE_SIZE);
    }

    /**
     * @brief Set Command Header
     * 
     * @param cls 
     * @param seq 
     * @param flg 
     * @param len 
     */
    inline void set_hdr(uint8_t cls, uint8_t seq, uint8_t flg, uint32_t len) {
        set_num(cls, seq);
        set_flg(flg);
        set_len(len);
    }

    cmd_res_t* res_;
}; // class Response

class MemoryArea {
public:
    /**
     * @brief 是否可直接用于DMA/RDMA传输
     * 
     * @return true 
     * @return false 
     */
    virtual bool is_dma() const = 0;

    /**
     * @brief 内存地址
     * 
     * @return void* 
     */
    virtual void* get_addr() const = 0;

    /**
     * @brief 内存地址（整型值）
     * 
     * @return uint64_t 
     */
    virtual uint64_t get_addr_uint64() const = 0;

    /**
     * @brief 内存区域长度
     * 
     * @return uint32_t 
     */
    virtual uint32_t get_len() const = 0;

    /**
     * @brief 密钥（RDMA）
     * 
     * @return uint32_t 
     */
    virtual uint32_t get_key() const = 0;

    /**
     * @brief 获取 cmd_ma_t 结构
     * 
     * @return cmd_ma_t 
     */
    virtual cmd_ma_t get() const = 0;

protected:
    MemoryArea() {}
    virtual ~MemoryArea() {}
};//class MemoryArea

typedef void(*cmd_cb_fn_t)(const Response& res, void* arg);
class RdmaWorkRequest;

struct MsgCallBack{
    cmd_cb_fn_t cb_fn;
    void* cb_arg;
};

class CmdClientStub {
public:
    // static std::shared_ptr<CmdClientStub> create_stub(std::string ip_addr, int port) = 0;
    virtual std::map<uint32_t, MsgCallBack>& get_cb_map() = 0;

    virtual int submit(RdmaWorkRequest& req, uint64_t io_addr, cmd_cb_fn_t cb_fn, void* cb_arg) = 0;
protected:
    CmdClientStub() {}
    ~CmdClientStub() {}
    // std::queue<MsgCallBack> msg_cb_q_;
    std::map<uint32_t, MsgCallBack> msg_cb_map_;
 
}; // class CommandStub 


class CmdService {
public:
    virtual int call(RdmaWorkRequest *req) = 0;

protected:
    CmdService() {}
    virtual ~CmdService() {}
}; // class CmdService

class CmdServiceMapper {
public:
    CmdServiceMapper(){}

    ~CmdServiceMapper(){}

    static CmdServiceMapper* g_cmd_service_mapper;

    static CmdServiceMapper* get_cmd_service_mapper(){
        if (g_cmd_service_mapper == nullptr) {
            g_cmd_service_mapper = new CmdServiceMapper();
        }
        return CmdServiceMapper::g_cmd_service_mapper; 
    }

    void register_service(uint8_t cmd_cls, uint8_t cmd_num, CmdService* svi);

    CmdService* get_service(uint8_t cmd_cls, uint8_t cmd_num);
private:
    std::map<uint16_t, CmdService*> service_mapper_;
}; // class CmdServiceMapper

class CmdServerStub {
public:
    
    int call_service();

protected:
    CmdServerStub() {}
    virtual ~CmdServerStub() {}
}; // class CmdServerStub


} // namespace flame

#endif // FLAME_INCLUDE_CMD_H
