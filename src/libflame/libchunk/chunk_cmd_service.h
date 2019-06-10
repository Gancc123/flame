/*
 * @Descripttion: 声明并定义四种对chunk的操作函数:read, write, read_zeros, write_zeros
 * @version: 
 * @Author: liweiguang
 * @Date: 2019-05-13 15:07:59
 * @LastEditors: Please set LastEditors
 * @LastEditTime: 2019-06-10 14:47:28
 */
#ifndef FLAME_LIBFLAME_LIBCHUNK_CHUNK_CMD_SERVICE_H
#define FLAME_LIBFLAME_LIBCHUNK_CHUNK_CMD_SERVICE_H
#include "libflame/libchunk/libchunk.h"

#include <iostream>
#include <cstring>

#include "include/csdc.h"
#include "include/retcode.h"
#include "msg/msg_core.h"
#include "libflame/libchunk/msg_handle.h"
#include "libflame/libchunk/log_libchunk.h"
#include "csd/csd_context.h"

namespace flame {

typedef void(*io_cb_fn_t)(RdmaWorkRequest* req); 

inline void io_cb_func(RdmaWorkRequest* req){
    req->status = RdmaWorkRequest::Status::EXEC_DONE;
    req->run();
    return ;
}


class ReadCmdService final : public CmdService {
public:
    inline int call(RdmaWorkRequest *req) override{
        msg::Connection* conn = req->conn;
        msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
        if(req->status == RdmaWorkRequest::Status::RECV_DONE){                 //**创建rdma内存，并从底层chunkstore异步读取数据到指定的rdma buffer**//      
            ChunkReadCmd* cmd_chunk_read = new ChunkReadCmd((cmd_t *)req->command);
            cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_read->get_content())->ma;    
            auto allocator = msg::Stack::get_rdma_stack()->get_rdma_allocator();
            
            msg::ib::RdmaBuffer* lbuf = allocator->alloc(cmd_chunk_read->get_ma_len()); //获取一片本地的内存，用于存放数据
            lbuf->data_len = cmd_chunk_read->get_ma_len();
            req->data_buf_ = lbuf;
            //read，将数据读到lbuf，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
            chunk_io_rw_mem(cmd_chunk_read->get_chk_id(),cmd_chunk_read->get_off(), cmd_chunk_read->get_ma_len(), (uint64_t)lbuf->buffer(), 0, io_cb_func, req); 

        }else if(req->status == RdmaWorkRequest::Status::EXEC_DONE){           //** 进行RDMA WRITE(server write到client相当于读)**//
            ChunkReadCmd* cmd_chunk_read = new ChunkReadCmd((cmd_t *)req->command);
            cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_read->get_content())->ma; 
            if(cmd_chunk_read->get_ma_len() > 4096){   //**利用WRITE
                req->sge_[0].addr = req->data_buf_->addr();
                req->sge_[0].length = req->data_buf_->size();
                req->sge_[0].lkey = req->data_buf_->lkey();
                ibv_send_wr &swr = req->send_wr_;
                memset(&swr, 0, sizeof(swr));
                swr.wr.rdma.remote_addr = ma.addr;
                swr.wr.rdma.rkey = ma.key;
                swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
                swr.opcode = IBV_WR_RDMA_WRITE;
                swr.send_flags |= IBV_SEND_SIGNALED;
                swr.num_sge = 1;
                swr.sg_list = req->sge_;
                swr.next = nullptr;

                rdma_conn->post_send(req);
            }else{                                      //**inline数据直接连带response send过去
                cmd_rc_t rc = 0;
                cmd_t cmd = *(cmd_t *)req->command;
                ChunkReadCmd* read_cmd = new ChunkReadCmd(&cmd); 
                cmd_res_t* cmd_res = (cmd_res_t *)req->command;
                ChunkReadRes* res = new ChunkReadRes(cmd_res, *read_cmd, rc, (void *)req->data_buf_->buffer(), cmd_chunk_read->get_ma_len()); 
                
                req->sge_[0].addr = req->buf_->addr();
                req->sge_[0].length = 64;
                req->sge_[0].lkey = req->buf_->lkey();

                req->sge_[1].addr = req->data_buf_->addr();
                req->sge_[1].length = cmd_chunk_read->get_ma_len();
                req->sge_[1].lkey = req->data_buf_->lkey();

                ibv_send_wr &swr = req->send_wr_;
                memset(&swr, 0, sizeof(swr));
                swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
                swr.opcode = IBV_WR_SEND;
                swr.send_flags |= IBV_SEND_SIGNALED;
                swr.num_sge = 2;
                swr.sg_list = req->sge_;
                swr.next = nullptr;

                rdma_conn->post_send(req);
            }
            

        }else if(req->status == RdmaWorkRequest::Status::WRITE_DONE){       
            cmd_rc_t rc = 0;
            cmd_t cmd = *(cmd_t *)req->command;
            ChunkReadCmd* read_cmd = new ChunkReadCmd(&cmd); 
            cmd_res_t* cmd_res = (cmd_res_t *)req->command;
            ChunkReadRes* res = new ChunkReadRes(cmd_res, *read_cmd, rc); 
            req->sge_[0].addr = req->buf_->addr();
            req->sge_[0].length = 64;
            req->sge_[0].lkey = req->buf_->lkey();
            req->send_wr_.opcode = IBV_WR_SEND;
            req->send_wr_.num_sge = 1;
            req->send_wr_.sg_list = req->sge_;
            rdma_conn->post_send(req);
        }else{                              
            return 0;
        }
        return 0;
    }

    ReadCmdService(CsdContext *cct):CmdService(), cct_(cct){}
    
    virtual ~ReadCmdService() {}
private:
    CsdContext cct_;

    /**
     * @name: chunk_io_rw_mem
     * @describtions:  模拟io从硬盘到RDMA内存的过程
     * @param   chk_id_t    chunk_id            chunk的id
     *          chk_off_t   offset              访问chunk的偏移
     *          uint32_t    len                 读/写长度
     *          uint64_t    laddr               本地的RDMA内存
     *          bool        rw                  判断 读/写 标志
     * @return: 
     */
    inline int chunk_io_rw_mem(chk_id_t chunk_id, chk_off_t offset, uint32_t len, uint64_t laddr, bool rw, io_cb_fn_t cb_fn, RdmaWorkRequest* cb_arg){
        if(rw){ //**write
            char *disk = new char[len];
            FlameContext* fct = FlameContext::get_context();
            fct->log()->ltrace("%c%c%c",((char *)laddr)[0], ((char *)laddr)[1], ((char *)laddr)[2]); 
            // memcpy(disk, (void *)laddr, len);
            std::shared_ptr<Chunk> chunk = cct_->cs()->chunk_open(123);

            chunk->write_async((void *)laddr, offset, len, cb_fn, (void *)cb_arg); 
            fct->log()->ltrace("write nvmestore done");
        }
        else{   //**read
            char *disk = new char[len];
            strcpy(disk,"abcdefghijklmnopq");
            memcpy((void *)laddr, disk, len);
        }
        return RC_SUCCESS;
    }
}; // class ReadCmdService


class WriteCmdService final : public CmdService {
public:
    inline int call(RdmaWorkRequest *req) override{
        msg::Connection* conn = req->conn;
        msg::RdmaConnection* rdma_conn = msg::RdmaStack::rdma_conn_cast(conn);
        if(req->status == RdmaWorkRequest::Status::RECV_DONE){                 //**创建rdma内存，并从底层chunkstore异步读取数据到指定的rdma buffer**//      
            ChunkWriteCmd* cmd_chunk_write = new ChunkWriteCmd((cmd_t *)req->command);
            if(cmd_chunk_write->get_inline_data_len() > 0){ //**inline的Write
                //write，将数据写到disk，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
                chunk_io_rw_mem(cmd_chunk_write->get_chk_id(),cmd_chunk_write->get_off(), cmd_chunk_write->get_ma_len(), (uint64_t)req->data_buf_->buffer(),\
                                                                     1, io_cb_func, req); 
                return 0;
            }
            cmd_ma_t& ma = ((cmd_chk_io_rd_t *)cmd_chunk_write->get_content())->ma;    
            auto allocator = msg::Stack::get_rdma_stack()->get_rdma_allocator();
            
            msg::ib::RdmaBuffer* lbuf = allocator->alloc(cmd_chunk_write->get_ma_len()); //获取一片本地的内存，用于存放数据
            lbuf->data_len = cmd_chunk_write->get_ma_len();
            req->data_buf_ = lbuf;

            req->sge_[0].addr = req->data_buf_->addr();
            req->sge_[0].length = req->data_buf_->size();
            req->sge_[0].lkey = req->data_buf_->lkey();
            ibv_send_wr &swr = req->send_wr_;
            memset(&swr, 0, sizeof(swr));
            swr.wr.rdma.remote_addr = ma.addr;
            swr.wr.rdma.rkey = ma.key;
            swr.wr_id = reinterpret_cast<uint64_t>((msg::RdmaSendWr *)req);
            swr.opcode = IBV_WR_RDMA_READ;
            swr.send_flags |= IBV_SEND_SIGNALED;
            swr.num_sge = 1;
            swr.sg_list = req->sge_;
            swr.next = nullptr;

            rdma_conn->post_send(req);

        }else if(req->status == RdmaWorkRequest::Status::READ_DONE){           //** 进行RDMA WRITE(server write到client相当于读)**//
            ChunkWriteCmd* cmd_chunk_write = new ChunkWriteCmd((cmd_t *)req->command);
            //write，将数据写到disk，io_cb_func的回调在这里实际上是在disk->lbuf后执行req->run()，只是此时req->status = EXEC_DONE
            chunk_io_rw_mem(cmd_chunk_write->get_chk_id(),cmd_chunk_write->get_off(), cmd_chunk_write->get_ma_len(), (uint64_t)req->data_buf_->buffer(),\
                                                                     1, io_cb_func, req); 

        }else if(req->status == RdmaWorkRequest::Status::EXEC_DONE){      
            cmd_rc_t rc = 0;
            cmd_t cmd = *(cmd_t *)req->command;
            ChunkWriteCmd* write_cmd = new ChunkWriteCmd(&cmd); 
            cmd_res_t* cmd_res = (cmd_res_t *)req->command;
            CommonRes* res = new CommonRes(cmd_res, *write_cmd, rc); 
            req->sge_[0].addr = req->buf_->addr();
            req->sge_[0].length = 64;
            req->sge_[0].lkey = req->buf_->lkey();
            req->send_wr_.opcode = IBV_WR_SEND;
            req->send_wr_.num_sge = 1;
            req->send_wr_.sg_list = req->sge_;
            rdma_conn->post_send(req);
        }else{                              
            return 0;
        }
        return 0;

    }

    WriteCmdService(CsdContext* cct) : CmdService(), cct_(cct){}

    virtual ~WriteCmdService() {}
private:
    CsdContext cct_;
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