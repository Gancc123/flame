/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:02:43
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-19 16:43:25
 */
/**
 * @file csdc.h
 * @author zhzane (zhzane@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2019-05-09
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef FLAME_INCLUDE_CSDC_H
#define FLAME_INCLUDE_CSDC_H

#include "include/cmd.h"
#include "libflame/libchunk/log_libchunk.h"
#include <memory.h>

#define MAX_INLINE_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

/**
 * @brief Chunk IO Commands
 * cls => CMD_CLS_IO_CHK (0xF0U)
 */
#define CMD_CHK_IO_INVALID 0x00U

/**
 * 关于 inline_data_len
 * Chunk Read响应 和 Write请求 可以直接在消息中携带数据（称为内带数据，即inline_data）
 * - inline_data_len 用于指明内带数据的长度
 * - 内带数据的位置必须从 Cache Line 对齐的位置开始
 * - cmd.hdr.len 包含 Command长度 和 内带数据长度
 */

/**
 * chunk io - read & write
 * @res: cmd_res_t
 */
#define CMD_CHK_IO_READ     0x01U
#define CMD_CHK_IO_WRITE    0x02U

struct cmd_chk_io_rd_t {
    chk_id_t    chk_id; // 8 B;
    uint64_t    off;    // 8 B;
    cmd_ma_t    ma;     // 16 B; 访问的数据大小由 dm.len 确定
} __attribute__ ((packed)); // 32 B;

struct res_chk_io_rd_t {
    uint32_t inline_data_len;
} __attribute__ ((packed));

struct cmd_chk_io_wr_t {
    chk_id_t    chk_id; // 8 B;
    uint64_t    off;    // 8 B;
    cmd_ma_t    ma;     // 16 B; 访问的数据大小由 dm.len 确定
    uint32_t    inline_data_len;   // 4 B;
} __attribute__ ((packed)); // 36 B;

/**
 * chunk io - set & reset
 * @res: cmd_res_t
 */
#define CMD_CHK_IO_SET      0x03U   // set every bit to 1
#define CMD_CHK_IO_RESET    0x04U   // set every bit to 0

struct cmd_chk_io_set_t {
    chk_id_t    chk_id; // 8 B;
    uint64_t    off;    // 8 B;
    uint32_t    len;    // 4 B;
} __attribute__((packed));  // 20 B;


#ifdef __cplusplus
}
#endif // __cplusplus

namespace flame {

class ChunkReadCmd : public Command {
public:
    ChunkReadCmd(cmd_t* rd_cmd) 
    : Command(rd_cmd), rd_((cmd_chk_io_rd_t*)get_content()) {}

    ChunkReadCmd(cmd_t* rd_cmd, uint64_t chk_id, uint64_t off, uint32_t len)
    : Command(rd_cmd), rd_((cmd_chk_io_rd_t*)get_content()) {
        set_hdr(CMD_CLS_IO_CHK, CMD_CHK_IO_READ, 0, sizeof(cmd_t));
        cmd_->hdr.cqg = CMD_CLS_CSD;
        rd_->chk_id = chk_id;
        rd_->off = off;
        rd_->ma.len = len;
    }

    ChunkReadCmd(cmd_t* rd_cmd, uint64_t chk_id, uint64_t off, uint32_t len, const MemoryArea& ma)
    : Command(rd_cmd), rd_((cmd_chk_io_rd_t*)get_content()) {
        set_hdr(CMD_CLS_IO_CHK, CMD_CHK_IO_READ, 0, sizeof(cmd_t));
        cmd_->hdr.cqg = CMD_CLS_CSD;
        rd_->chk_id = chk_id;
        rd_->off = off;
        rd_->ma.addr = ma.get_addr_uint64();
        rd_->ma.len = len < ma.get_len() ? len : ma.get_len();
        rd_->ma.key = ma.get_key();
    }

    ~ChunkReadCmd() {}

    inline void copy(void* buff) {
        memcpy(buff, (void*)&cmd_, sizeof(cmd_t));
    }

    inline uint64_t get_chk_id() const { return rd_->chk_id; }

    inline uint64_t get_off() const { return rd_->off; }

    inline uint32_t get_ma_len() const { return rd_->ma.len; }

    inline uint64_t get_ma_addr() const { return rd_->ma.addr; }

    inline uint32_t get_ma_key() const { return rd_->ma.key; }

private:
    cmd_chk_io_rd_t* rd_;
}; // class ChunkReadCmd

class ChunkWriteCmd : public Command {
public:
    ChunkWriteCmd(cmd_t* cmdp)
    : Command(cmdp), wr_((cmd_chk_io_wr_t*)get_content()) {}

    ChunkWriteCmd(cmd_t* cmdp, uint64_t chk_id, uint64_t off, uint32_t len, const MemoryArea&  ma, bool force_inline)
    : Command(cmdp), wr_((cmd_chk_io_wr_t*)get_content()) {
        set_hdr(CMD_CLS_IO_CHK, CMD_CHK_IO_WRITE, 0, sizeof(cmd_t));
        cmd_->hdr.cqg = CMD_CLS_CSD;
        wr_->chk_id = chk_id;
        wr_->off = off;
        wr_->ma.addr = ma.get_addr_uint64();
        wr_->ma.len = len < ma.get_len() ? len : ma.get_len();
        wr_->ma.key = ma.get_key();
        

        if (force_inline && ma.is_dma() && ma.get_len() <= MAX_INLINE_SIZE) { // 内联数据传递，跟随request一起传输
            wr_->inline_data_len = len < ma.get_len() ? len : ma.get_len();
            inline_data = ma.get_addr();
        }
        else{
            wr_->inline_data_len = 0;
        }
    }

    ~ChunkWriteCmd() {}

    inline void copy(void* buff) {}
    virtual void copy(void* header, void* inline_data = nullptr) {
        memcpy(header, cmd_, sizeof(cmd_t));
        if (get_inline_data_len() != 0 && inline_data) {
            memcpy(inline_data, (void*)get_ma_addr(), get_inline_data_len());
        }
    }

    inline uint64_t get_chk_id() const { return wr_->chk_id; }

    inline uint64_t get_off() const { return wr_->off; }

    inline uint32_t get_ma_len() const { return wr_->ma.len; }

    inline uint64_t get_ma_addr() const { return wr_->ma.addr; }

    inline uint32_t get_ma_key() const { return wr_->ma.key; }

    inline uint32_t get_inline_data_len() const { return wr_->inline_data_len; }

    inline void* get_inline_data_addr() const { return inline_data; }

private:
    cmd_chk_io_wr_t* wr_;
    void* inline_data;
    
}; // class ChunkWriteCmd

class ChunkSetCmd : Command {
public:
    ChunkSetCmd(cmd_t* cmdp) 
    : Command(cmdp), set_((cmd_chk_io_set_t*)get_content()) {}

    ChunkSetCmd(cmd_t* cmdp, uint64_t chk_id, uint64_t off, uint32_t len)
    : Command(cmdp), set_((cmd_chk_io_set_t*)get_content()) {
        set_hdr(CMD_CLS_IO_CHK, CMD_CHK_IO_SET, 0, sizeof(cmd_t));
        cmd_->hdr.cqg = CMD_CLS_CSD;
        set_->chk_id = chk_id;
        set_->off = off;
        set_->len = len;
    }

    ~ChunkSetCmd() {}

    void copy(void* buff) {
        memcpy(buff, (void*)cmd_, sizeof(cmd_t));
    }

    inline uint64_t get_chk_id() const { return set_->chk_id; }

    inline uint64_t get_off() const { return set_->off; }

    inline uint32_t get_set_len() const { return set_->len; }

private:
    cmd_chk_io_set_t* set_;
}; // class ChunkSetCmd

class ChunkResetCmd : Command {
public:
    ChunkResetCmd(cmd_t* cmdp) 
    : Command(cmdp), reset_((cmd_chk_io_set_t*)get_content()) {}

    ChunkResetCmd(cmd_t* cmdp, uint64_t chk_id, uint64_t off, uint32_t len)
    : Command(cmdp), reset_((cmd_chk_io_set_t*)get_content()) {
        set_hdr(CMD_CLS_IO_CHK, CMD_CHK_IO_RESET, 0, sizeof(cmd_t));
        cmd_->hdr.cqg = CMD_CLS_CSD;
        reset_->chk_id = chk_id;
        reset_->off = off;
        reset_->len = len;
    }

    ~ChunkResetCmd() {}

    // inline void copy(void* buff) {
    //     memcpy(buff, (void*)&set_cmd_, sizeof(cmd_t));
    // }

    inline uint64_t get_chk_id() const { return reset_->chk_id; }

    inline uint64_t get_off() const { return reset_->off; }

    inline uint32_t get_reset_len() const { return reset_->len; }

private:
    cmd_chk_io_set_t* reset_;
}; // class ChunkSetCmd

class CommonRes : public Response {
public:
    CommonRes(cmd_res_t* res)
    : Response(res){}

    CommonRes(cmd_res_t* res, const Command& command, cmd_rc_t rc)
    : Response(res) {
        cpy_hdr(command);
        res_->hdr.cqg = CMD_CLS_CSD;
        set_len(sizeof(cmd_res_t));
        set_rc(rc);
    }

    ~CommonRes() {}

    inline void copy(void* buff) {
        memcpy(buff, res_, sizeof(cmd_res_t));
    }
}; // class CommonRes

class ChunkReadRes : public Response {
public:
    ChunkReadRes(cmd_res_t* res)
    : Response(res), rd_((res_chk_io_rd_t*)res->cont){}

    ChunkReadRes(cmd_res_t* res, const Command& command, cmd_rc_t rc)
    : Response(res), inline_data_(nullptr), rd_((res_chk_io_rd_t*)res_->cont) {
        cpy_hdr(command);
        res_->hdr.cqg = CMD_CLS_CSD;
        set_len(sizeof(cmd_res_t));
        set_rc(rc);
        rd_->inline_data_len = 0;
    }

    ChunkReadRes(cmd_res_t* res, const Command& command, cmd_rc_t rc, void* inline_buff, uint32_t len)
    : Response(res), inline_data_(inline_buff), rd_((res_chk_io_rd_t*)res_->cont) {
        cpy_hdr(command);
        res_->hdr.cqg = CMD_CLS_CSD;
        set_len(sizeof(cmd_res_t));
        set_rc(rc);
        rd_->inline_data_len = len;
    }

    ~ChunkReadRes() {}

    inline void copy(void* buff, void* inline_buf = nullptr) {
        memcpy(buff, res_, sizeof(cmd_res_t));
        if (inline_data_ && inline_buf) {
            memcpy(inline_buf, inline_data_, rd_->inline_data_len);
        }
    }

    inline uint32_t get_inline_len() const {return rd_->inline_data_len;}

private:
    res_chk_io_rd_t* rd_;
    void* inline_data_;
}; // class ChunkReadRes

} // namespace flame


#endif // !FLAME_INCLUDE_CSDC_H