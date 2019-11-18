/*
 * @Descripttion: 声明并定义四种对chunk的操作函数:read, write, read_zeros, write_zeros
 * @version: 
 * @Author: liweiguang
 * @Date: 2019-05-13 15:07:59
 * @LastEditors: lwg
 * @LastEditTime: 2019-11-15 15:22:42
 */
#ifndef FLAME_LIBFLAME_LIBCHUNK_CHUNK_CMD_SERVICE_H
#define FLAME_LIBFLAME_LIBCHUNK_CHUNK_CMD_SERVICE_H
#include "libflame/libchunk/libchunk.h"

#include <iostream>
#include <cstring>

#include "csd/csd_context.h"
#include "chunkstore/chunkstore.h"

#define IO_READ  0
#define IO_WRITE  1

namespace flame {

struct Iocb{
    RdmaWorkRequest* req;
    std::shared_ptr<ChunkStore> chunkstore;
    std::shared_ptr<Chunk> chunk;
    Iocb(std::shared_ptr<ChunkStore> _chunkstore, std::shared_ptr<Chunk> _chunk, RdmaWorkRequest* _req)
    : chunkstore(_chunkstore), chunk(_chunk), req(_req) { 
    }
};

class ReadCmdService final : public CmdService {
public:
    int call(RdmaWorkRequest *req) override;

    ReadCmdService(CsdContext *cct):CmdService(), cct_(cct){}
    virtual ~ReadCmdService() {}
private:
    CsdContext* cct_;
    int _chunk_io_rw(std::shared_ptr<Chunk> chunk, chk_off_t offset, uint32_t len, uint64_t laddr, bool rw, chunk_opt_cb_t cb_fn, void* cb_arg);
    void _set_seg(ibv_sge& sge, uint64_t addr, size_t size, uint32_t lkey);
    void _prepare_send(RdmaWorkRequest *req, int num_sge);
    void _prepare_write(RdmaWorkRequest *req, cmd_ma_t remote_ma);
}; // class ReadCmdService


class WriteCmdService final : public CmdService {
public:
    int call(RdmaWorkRequest *req) override;

    WriteCmdService(CsdContext* cct) : CmdService(), cct_(cct){}
    virtual ~WriteCmdService() {}
private:
    CsdContext* cct_;
    int _chunk_io_rw(std::shared_ptr<Chunk> chunk, chk_off_t offset, uint32_t len, uint64_t laddr, bool rw, chunk_opt_cb_t cb_fn, void* cb_arg);
    void _set_seg(ibv_sge& sge, uint64_t addr, size_t size, uint32_t lkey);
    void _prepare_send(RdmaWorkRequest *req, int num_sge);
    void _prepare_read(RdmaWorkRequest *req, cmd_ma_t remote_ma);
}; // class WriteCmdService

class ReadZerosCmdService final : public CmdService {
public:
    inline virtual int call(RdmaWorkRequest *req) override{
        return 0;
    }
    ReadZerosCmdService() : CmdService(){}
    virtual ~ReadZerosCmdService() {}
}; // class ReadZerosCmdService


class WriteZerosCmdService final : public CmdService {
public:
    inline virtual int call(RdmaWorkRequest *req) override{
        return 0;
    }
    WriteZerosCmdService() : CmdService(){}
    virtual ~WriteZerosCmdService() {}
}; // class WriteZerosCmdService
} // namespace flame

#endif //FLAME_LIBFLAME_LIBCHUNK_CHUNK_CMD_SERVICE_H