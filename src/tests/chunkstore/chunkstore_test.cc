#include "common/context.h"
#include "common/cmdline.h"
#include "common/convert.h"
#include "common/thread/thread.h"

#include "chunkstore/cs.h"
#include "chunkstore/log_cs.h"
#include "chunkstore/nvmestore/nvmestore.h"

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/blob_bdev.h"
#include "spdk/blob.h"
#include "spdk/log.h"
#include "spdk/version.h"
#include "spdk/string.h"

#include <memory>
#include <string>

#define LINE_LENGTH 128
#define CHUNK_ID 123
#define cs_url "nvmestore://Nvme0n1:/etc/flame/nvmestore.conf"

using namespace std;
using namespace flame;
using namespace flame::cli;

struct request {
    uint64_t chunk_id;
    uint64_t offset;
    uint64_t length;
    int op;
    void *buffer;
public:
    request(uint64_t _chid, uint64_t _off, uint64_t _len, int _op, void *_buf)
    : chunk_id(_chid), offset(_off),length(_len), op(_op), buffer(_buf){

    }

};

struct IOCB : public request {
    std::shared_ptr<ChunkStore> cs;
    std::shared_ptr<Chunk> chunk;
public:
    IOCB(std::shared_ptr<ChunkStore> _cs, std::shared_ptr<Chunk> _chk, uint64_t _chid, uint64_t _off, uint64_t _len, int _op, void *_buf)
    : cs(_cs), chunk(_chk), request(_chid, _off, _len,  _op, _buf) {

    }
};

void read_call_back(void *arg) {
    struct IOCB *iocb = (struct IOCB *)arg;
    //NvmeStore *nvmestore = req->nvmestore;

    //req->complete_tsc = spdk_get_ticks();
    for(size_t i = 0; i < 4096; i++) {
        std::cout << ((char*)(iocb->buffer))[i];
    }
    std::cout << "\n";

    if(iocb->buffer)
        spdk_dma_free(iocb->buffer);
    


    NvmeChunk *nvme_chunk = dynamic_cast<NvmeChunk *>(iocb->chunk.get());
    NvmeStore *nvmestore = dynamic_cast<NvmeStore *>(iocb->cs.get());

    nvmestore->chunk_close(nvme_chunk);
    //nvme_chunk->close();

    iocb->cs->dev_unmount();
    spdk_app_stop(0);
    //nvmestore->signal_io_completed();
}
 
void write_call_back(void *arg) {
    struct IOCB *iocb = (struct IOCB *)arg;
    
    if(iocb->buffer) {
        memset(iocb->buffer, 0, 4096);
    }

    std::shared_ptr<Chunk> chunk = iocb->chunk;
    chunk->read_async(iocb->buffer, 0, 4096, ::read_call_back, iocb);
}

void test_start(void *arg1, void *arg2) {
    FlameContext *fct = (FlameContext *)arg1;

    std::shared_ptr<ChunkStore> cs = create_chunkstore(fct, cs_url);
    if(!cs) {
        fct->log()->lerror("create chunkstore failed.");
        return ;
    }

    int r = cs->dev_check();

    switch (r) {
    case ChunkStore::DevStatus::NONE:
        fct->log()->lerror("device not existed");
        return ;
    case ChunkStore::DevStatus::UNKNOWN:
        fct->log()->lwarn("unknown device format");
        break;
    case ChunkStore::DevStatus::CLT_OUT:
        fct->log()->lwarn("the divice belong to other cluster");
        break;
    case ChunkStore::DevStatus::CLT_IN:
        break;
    }

    if((r = cs->dev_format()) != 0) {
        fct->log()->lerror("format chunkstore failed.");
        return ;
    }
    fct->log()->linfo("");

    if((r = cs->dev_mount()) != 0) {
        fct->log()->lerror("mount chunkstore failed.");
        return ;
    }
    fct->log()->linfo("mount chunkstore success.");

    int rc;
    chunk_create_opts_t opts;
    opts.set_prealloc(true);
   
    cs->chunk_create(CHUNK_ID, opts);

    dynamic_pointer_cast<NvmeStore>(cs)->print_store();

    std::shared_ptr<Chunk> chunk = cs->chunk_open(CHUNK_ID);

    void *buffer = spdk_dma_malloc(4096, 4096, NULL);
    if(!buffer) {
        fct->log()->lerror("malloc failed.");
        spdk_app_stop(-1);
    }

    memset(buffer, 'c', 4096);

    //struct request *req=new request(CHUNK_ID, 0, 4096, 1, buffer);
    struct IOCB *ioc=new IOCB(cs, chunk, CHUNK_ID, 0, 4096, 1, buffer );
   
    //chunk->write_async(buffer, 0, 4096, ::write_call_back, (void *)chunk);
    chunk->write_async(buffer, 0, 4096, ::write_call_back, ioc);
}

int main(int argc, char *argv[]) {

    FlameContext *fct = FlameContext::get_context();
    if(!fct->init_log("./", "DEBUG", "chunkstore")) {
        //fct->log()->lerror("init log failed.");
        std::cout << "init log failed." << std::endl;
        return -1;
    }
    // 初始化spdk应用程序启动的配置参数
    struct spdk_app_opts opts = {};
    int rc = 0;
    spdk_app_opts_init(&opts);

    opts.name = "chunkstore_test";
    opts.reactor_mask = "0x0f";
    opts.config_file = "/etc/flame/nvme.conf";

    std::cout << "opt init completed." << std::endl;

    rc = spdk_app_start(&opts, test_start, fct, nullptr);
    if(rc) {
        SPDK_NOTICELOG("spdk app start: ERROR!\n");
    } else {
        SPDK_NOTICELOG("SUCCESS.\n");
    }

    spdk_app_fini();

    return 0;
}