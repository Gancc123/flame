/*
 * @Descripttion: 声明并定义四种对chunk的操作函数:read, write, read_zeros, write_zeros
 * @version: 
 * @Author: liweiguang
 * @Date: 2019-05-13 15:07:59
 * @LastEditors: lwg
 * @LastEditTime: 2019-10-08 09:50:21
 */

#include "libflame/libchunk/chunk_cmd_service.h"

#include "include/csdc.h"
#include "include/retcode.h"
#include "msg/msg_core.h"
#include "libflame/libchunk/msg_handle.h"
#include "libflame/libchunk/log_libchunk.h"

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/syscall.h>

#define gettid() syscall(SYS_gettid)

namespace flame {

void io_cb_func(void* argc){
    FlameContext* fct = FlameContext::get_context();
    fct->log()->ldebug("nvmestore handled done");
    Iocb* iocb = (Iocb*)argc;
    RdmaWorkRequest* req = iocb->req;
    req->status = RdmaWorkRequest::Status::EXEC_DONE;
    req->run();
    return ;
}

void ReadCmdService::_set_seg(ibv_sge& sge, uint64_t addr, size_t size, uint32_t lkey){
    sge.addr = addr;
    sge.length = size;
    sge.lkey = lkey;
}

void ReadCmdService::_prepare_write(RdmaWorkRequest *req, cmd_ma_t remote_ma){
    ibv_send_wr &swr = req->send_wr_;
    memset(&swr, 0, sizeof(swr));
    swr.wr.rdma.remote_addr = remote_ma.addr;
    swr.wr.rdma.rkey = remote_ma.key;
    swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
    swr.opcode = IBV_WR_RDMA_WRITE;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = 1;
    swr.sg_list = req->sge_;
    swr.next = nullptr;
}

void ReadCmdService::_prepare_send(RdmaWorkRequest *req, int num_sge){
    ibv_send_wr &swr = req->send_wr_;
    memset(&swr, 0, sizeof(swr));
    swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
    swr.opcode = IBV_WR_SEND;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = num_sge;
    swr.sg_list = req->sge_;
    swr.next = nullptr;
}

int ReadCmdService::call(RdmaWorkRequest *req){
    msg::Connection* conn = req->conn;
    msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
    if(req->status == RdmaWorkRequest::Status::RECV_DONE){                 //**创建rdma内存，并从底层chunkstore异步读取数据到指定的rdma buffer**//      
        ChunkReadCmd* cmd_chunk_read = new ChunkReadCmd((cmd_t *)req->command);
        cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_read->get_content())->ma;  
        //read，将数据读到lbuf，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
        std::shared_ptr<ChunkStore> chunkstore = cct_->cs();
        FlameContext* flame_context = FlameContext::get_context();
        flame_context->log()->ldebug("now tid is %d, cmd_chunk_read->get_chk_id() = %d", gettid(), cmd_chunk_read->get_chk_id());
        flame_context->log()->ldebug("cmd_chunk_read->get_off() = 0x%x", cmd_chunk_read->get_off());
        flame_context->log()->ldebug("cmd_chunk_read->get_ma_len() = 0x%x", cmd_chunk_read->get_ma_len());

        std::shared_ptr<Chunk> chunk = chunkstore->chunk_open(cmd_chunk_read->get_chk_id());
        Iocb* iocb = new Iocb(chunkstore, chunk, req);
        BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
        if(cmd_chunk_read->get_ma_len() > MAX_INLINE_SIZE){
            Buffer* lbuf = allocator->allocate_ptr(cmd_chunk_read->get_ma_len()); //获取一片本地的内存，用于存放数据
            req->data_buf_ = lbuf;
            _chunk_io_rw(chunk, cmd_chunk_read->get_off(), cmd_chunk_read->get_ma_len(), (uint64_t)req->data_buf_->addr(), IO_READ, io_cb_func, iocb); 
        } else {
            _chunk_io_rw(chunk, cmd_chunk_read->get_off(), cmd_chunk_read->get_ma_len(), (uint64_t)req->buf_[1]->addr(), IO_READ, io_cb_func, iocb); 
        }   
    }else if(req->status == RdmaWorkRequest::Status::EXEC_DONE){           //** 进行RDMA WRITE(server write到client相当于读)**//
        ChunkReadCmd* cmd_chunk_read = new ChunkReadCmd((cmd_t *)req->command);
        cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_read->get_content())->ma; 
        if(cmd_chunk_read->get_ma_len() > MAX_INLINE_SIZE){   //**WRITE
            _set_seg(req->sge_[0], (uint64_t)req->data_buf_->addr(), req->data_buf_->size(), req->data_buf_->lkey());
            _prepare_write(req, ma);
            rdma_conn->post_send(req);
        }else{//**inline数据直接连带response SEND过去
            cmd_rc_t rc = 0;
            cmd_t cmd = *(cmd_t *)req->command;
            ChunkReadCmd* read_cmd = new ChunkReadCmd(&cmd); 
            cmd_res_t* cmd_res = (cmd_res_t *)req->command;
            ChunkReadRes* res = new ChunkReadRes(cmd_res, *read_cmd, rc, req->buf_[0]->addr(), cmd_chunk_read->get_ma_len()); 
            _set_seg(req->sge_[0], (uint64_t)req->buf_[0]->addr(), 64, req->buf_[0]->lkey());
            _set_seg(req->sge_[1], (uint64_t)req->buf_[1]->addr(), MAX_INLINE_SIZE, req->buf_[1]->lkey());
            _prepare_send(req, 2);
            rdma_conn->post_send(req);
        }
    }else if(req->status == RdmaWorkRequest::Status::WRITE_DONE){ 
        if(req->get_data_buf()) delete(req->get_data_buf());    
        cmd_rc_t rc = 0;
        cmd_t cmd = *(cmd_t *)req->command;
        ChunkReadCmd* read_cmd = new ChunkReadCmd(&cmd); 
        cmd_res_t* cmd_res = (cmd_res_t *)req->command;
        ChunkReadRes* res = new ChunkReadRes(cmd_res, *read_cmd, rc); 
        _set_seg(req->sge_[0], (uint64_t)req->buf_[0]->addr(), 64, req->buf_[0]->lkey());
        _prepare_send(req, 1);
        rdma_conn->post_send(req);
    }else{                              
        return 1;
    }
    return 0;
}
/**
 * @name: _chunk_io_rw
 * @describtions:  调用chunkstore接口进行实际写盘
 * @param   chk_id_t    chunk_id            chunk的id
 *          chk_off_t   offset              访问chunk的偏移
 *          uint32_t    len                 读/写长度
 *          uint64_t    laddr               本地的RDMA内存
 *          bool        rw                  判断 读/写 标志
 * @return: 
 */
int ReadCmdService::_chunk_io_rw(std::shared_ptr<Chunk> chunk, chk_off_t offset, uint32_t len, uint64_t laddr, bool rw, chunk_opt_cb_t cb_fn, void* cb_arg){
    FlameContext* fct = FlameContext::get_context();
    if(rw){ //**write
        fct->log()->ltrace("write offset = 0x%x, len = 0x%x", offset, len); 
        chunk->write_async((void *)laddr, offset, len, cb_fn, cb_arg); 
    }
    else{   //**read
        chunk->read_async((void *)laddr, offset, len, cb_fn, cb_arg);
        fct->log()->ltrace("read offset = 0x%x, len = 0x%x", offset, len); 
    }
    return RC_SUCCESS;
}

void WriteCmdService::_set_seg(ibv_sge& sge, uint64_t addr, size_t size, uint32_t lkey){
    sge.addr = addr;
    sge.length = size;
    sge.lkey = lkey;
}

void WriteCmdService::_prepare_read(RdmaWorkRequest *req, cmd_ma_t remote_ma){
    ibv_send_wr &swr = req->send_wr_;
    memset(&swr, 0, sizeof(swr));
    swr.wr.rdma.remote_addr = remote_ma.addr;
    swr.wr.rdma.rkey = remote_ma.key;
    swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
    swr.opcode = IBV_WR_RDMA_READ;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = 1;
    swr.sg_list = req->sge_;
    swr.next = nullptr;
}

void WriteCmdService::_prepare_send(RdmaWorkRequest *req, int num_sge){
    ibv_send_wr &swr = req->send_wr_;
    memset(&swr, 0, sizeof(swr));
    swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
    swr.opcode = IBV_WR_SEND;
    swr.send_flags |= IBV_SEND_SIGNALED;
    swr.num_sge = num_sge;
    swr.sg_list = req->sge_;
    swr.next = nullptr;
}

int WriteCmdService::call(RdmaWorkRequest *req){
    msg::Connection* conn = req->conn;
    msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
    if(req->status == RdmaWorkRequest::Status::RECV_DONE){                 /**创建rdma内存，并从底层chunkstore异步读取数据到指定的rdma buffer**/      
        ChunkWriteCmd* cmd_chunk_write = new ChunkWriteCmd((cmd_t *)req->command);
        if(cmd_chunk_write->get_inline_data_len() > 0){ //**inline的Write
            //write，将数据写到disk，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
            std::shared_ptr<Chunk> chunk = cct_->cs()->chunk_open(cmd_chunk_write->get_chk_id());
            Iocb* iocb = new Iocb(cct_->cs(), chunk, req);
            _chunk_io_rw(chunk, cmd_chunk_write->get_off(), cmd_chunk_write->get_ma_len(), (uint64_t)req->buf_[1]->addr(),\
                            IO_WRITE, io_cb_func, iocb); 
            return 0;
        }
        cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_write->get_content())->ma;   
        BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator();
        Buffer* lbuf = allocator->allocate_ptr(cmd_chunk_write->get_ma_len()); //获取一片本地的内存，用于存放数据
        req->data_buf_ = lbuf;
        _set_seg(req->sge_[0], (uint64_t)req->data_buf_->addr(), req->data_buf_->size(), req->data_buf_->lkey());
        _prepare_read(req, ma);
        rdma_conn->post_send(req);
    }else if(req->status == RdmaWorkRequest::Status::READ_DONE){           //** 进行RDMA WRITE(server write到client相当于读)**//
        ChunkWriteCmd* cmd_chunk_write = new ChunkWriteCmd((cmd_t *)req->command);
        //write，将数据写到disk，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
        std::shared_ptr<Chunk> chunk = cct_->cs()->chunk_open(cmd_chunk_write->get_chk_id());
        Iocb* iocb = new Iocb(cct_->cs(), chunk, req);
        _chunk_io_rw(chunk, cmd_chunk_write->get_off(), cmd_chunk_write->get_ma_len(), (uint64_t)req->data_buf_->addr(),\
                            IO_WRITE, io_cb_func, iocb); 
    }else if(req->status == RdmaWorkRequest::Status::EXEC_DONE){    
        if(req->get_data_buf()) delete(req->get_data_buf());  
        cmd_rc_t rc = 0;
        cmd_t cmd = *(cmd_t *)req->command;
        ChunkWriteCmd* write_cmd = new ChunkWriteCmd(&cmd); 
        cmd_res_t* cmd_res = (cmd_res_t *)req->command;
        CommonRes* res = new CommonRes(cmd_res, *write_cmd, rc); 
        _set_seg(req->sge_[0], (uint64_t)req->buf_[0]->addr(), 64, req->buf_[0]->lkey());
        _prepare_send(req, 1);
        rdma_conn->post_send(req);
    }else{                              
        return 0;
    }
    return 0;
}

int WriteCmdService::_chunk_io_rw(std::shared_ptr<Chunk> chunk, chk_off_t offset, uint32_t len, uint64_t laddr, bool rw, chunk_opt_cb_t cb_fn, void* cb_arg){
    FlameContext* fct = FlameContext::get_context();
    fct->log()->ltrace("chunk addr:%x",chunk.get()); 
    if(rw){ //**write
        fct->log()->ltrace("write offset = %u, len = %u", offset, len); 
        chunk->write_async((void *)laddr, offset, len, cb_fn, cb_arg); 
    }
    else{   //**read
        chunk->read_async((void *)laddr, offset, len, cb_fn, cb_arg);
        fct->log()->ltrace("read offset = %u, len = %u", offset, len); 
    }
    return RC_SUCCESS;
}
} // namespace flame