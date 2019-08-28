/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:02:44
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-20 10:29:04
 */
#include <unistd.h>
#include <cstdio>

#include "libflame/libchunk/libchunk.h"
#include "include/csdc.h"
#include "libflame/libchunk/log_libchunk.h"

#include "common/context.h"
#include "common/log.h"
#include "util/spdk_common.h"
#include "memzone/rdma_mz.h"
#include "util/ip_op.h"

using namespace flame;
using namespace flame::memory;
using namespace flame::memory::ib;
using namespace flame::msg;
using FlameContext = flame::FlameContext;

#define CFG_PATH "flame_client.cfg"


void cb_func(const Response& res, uint64_t bufaddr, void* arg){
    char* mm = (char*)arg;
    std::cout << mm[0] << mm[1] << mm[2] << std::endl; 
    std::cout << "111" << std::endl;
    return ;
}

void cb_func2(const Response& res, uint64_t bufaddr, void* arg){
    std::cout << "222" << std::endl;
    return ;
}


static void msg_stop(void *arg1, void *arg2){
    MsgContext *mct = (MsgContext *)arg1;
    mct->finally_fin();
    spdk_app_stop(0);
}

static void test_libchunk(void *arg1, void *arg2){
    FlameContext *flame_context = FlameContext::get_context();
    if(!flame_context->init_config(CFG_PATH)){
        clog("init config failed.");
        return ;
    }
    if(!flame_context->init_log("", "TRACE", "client")){
        clog("init log failed.");
        return ;
    }

    // MemoryConfig *mem_cfg = MemoryConfig::load_config(flame_context);
    // assert(mem_cfg);
    std::cout << "load config completed.." << std::endl;

    std::shared_ptr<CmdClientStubImpl> cmd_client_stub = CmdClientStubImpl::create_stub(msg_stop);
    cmd_client_stub->set_session("192.168.3.112", 9999);
    std::string ip_string = "192.168.3.112";
    uint64_t peer_ip = string_to_ip(ip_string);
    uint64_t peer_io_addr = peer_ip << 16 | (uint32_t)9999;
    BufferAllocator *allocator = RdmaAllocator::get_buffer_allocator(); 

    /* 无inline的写入chunk的操作 */
    RdmaWorkRequest* rdma_work_request_write= cmd_client_stub->get_request();
    Buffer buf_write = allocator->allocate(1 << 22); //4MB
    char a[10] = "123456789";
    memcpy(buf_write.addr(), a, 8);
    MemoryArea* memory_write = new MemoryAreaImpl((uint64_t)buf_write.addr(), (uint32_t)buf_write.size(), buf_write.rkey(), 1);
    cmd_t* cmd_write = (cmd_t *)rdma_work_request_write->command;
    ChunkWriteCmd* write_cmd = new ChunkWriteCmd(cmd_write, 1048592, 0, 4096, *memory_write, 0); 
    cmd_client_stub->submit(*rdma_work_request_write, peer_io_addr, &cb_func2, 0, nullptr);  //**写成功是不需要第三个参数的，只需要返回response

    /* 无inline的读取chunk的操作 */
    RdmaWorkRequest* rdma_work_request_read = cmd_client_stub->get_request();
    Buffer buf_read = allocator->allocate(1 << 22); //4MB
    MemoryArea* memory_read = new MemoryAreaImpl((uint64_t)buf_read.addr(), (uint32_t)buf_read.size(), buf_read.rkey(), 1);
    cmd_t* cmd_read = (cmd_t *)rdma_work_request_read->command;
    ChunkReadCmd* read_cmd = new ChunkReadCmd(cmd_read, 1048592, 0, 8192, *memory_read); 
    cmd_client_stub->submit(*rdma_work_request_read, peer_io_addr, &cb_func, (uint64_t)buf_read.addr(), buf_read.addr());

    /* 带inline的写入chunk的操作 */
    RdmaWorkRequest* rdma_work_request_write_inline = cmd_client_stub->get_request();
    cmd_t* cmd_write_inline = (cmd_t *)rdma_work_request_write_inline->command;
    Buffer buf_write_inline = rdma_work_request_write_inline->get_data_buf();
    char b[8] = "9876543";
    memcpy(buf_write_inline.addr(), b, 8);
    MemoryArea* memory_write_inline = new MemoryAreaImpl((uint64_t)buf_write_inline.addr(),\
                     (uint32_t)buf_write_inline.size(), buf_write_inline.rkey(), 1);
    ChunkWriteCmd* write_cmd_inline = new ChunkWriteCmd(cmd_write_inline, 1048592, 0, 4096, *memory_write_inline, 1); 
    cmd_client_stub->submit(*rdma_work_request_write_inline, peer_io_addr, &cb_func2, 0, nullptr); //**这里第三个参数写nullptr但是在内部调用时因为时inline_read，会自动将参数定位到recv的rdma buffer上

    /* 带inline的读取chunk的操作 */
    RdmaWorkRequest* rdma_work_request_read_inline = cmd_client_stub->get_request();
    cmd_t* cmd_read_inline = (cmd_t *)rdma_work_request_read_inline->command;
    ChunkReadCmd* read_cmd_inline = new ChunkReadCmd(cmd_read_inline, 1048592, 0, 4096); 
    cmd_client_stub->submit(*rdma_work_request_read_inline, peer_io_addr, &cb_func, 0, nullptr); //**这里第三个参数写nullptr但是在内部调用时因为时inline_read，会自动将参数定位到recv的rdma buffer上


    std::getchar();
    flame_context->log()->ltrace("Start to exit!");
    buf_write.clear();
    buf_read.clear();
    buf_write_inline.clear();
}

int main(int argc, char *argv[])
{
    int rc = 0;
    struct spdk_app_opts opts = {};
    spdk_app_opts_init(&opts);
    opts.name = "libchunk_test";
    opts.reactor_mask = "0xf";
    opts.rpc_addr = "/var/tmp/spdk_libchunk_c.sock";

    rc = spdk_app_start(&opts, test_libchunk, nullptr, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS!\n");
    }

    spdk_app_fini();

    return rc;
}