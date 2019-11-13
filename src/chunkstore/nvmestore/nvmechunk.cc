/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-09-04 15:20:04
 * @LastEditors: lwg
 * @LastEditTime: 2019-09-29 17:11:00
 */
#include "chunkstore/nvmestore/nvmestore.h"
#include "util/utime.h"
#include "chunkstore/log_cs.h"

using namespace flame;

int NvmeChunk::get_info(chunk_info_t& info) const {
    info.chk_id = chk_id;
    info.vol_id = volume_id;
    info.index = volume_index;
    info.stat = chunk_stat;
    info.spolicy = chunk_spolicy;
    info.flags = flags;
    info.size = chunk_size;
    info.used = chunk_used;
    info.ctime = ctime;
    info.dst_id = dst_id;
    info.dst_ctime = dst_ctime;
}

void NvmeChunk::set_dirty() {
    dirty = true;
}

void NvmeChunk::reset_dirty() {
    dirty = false;
}

bool NvmeChunk::is_dirty() {
    return dirty;
}

void NvmeChunk::open_ref_add(uint64_t val) {
    open_ref += val;
}

void NvmeChunk::open_ref_sub(uint64_t val) {
    open_ref -= val;
}

uint64_t NvmeChunk::get_open_ref() const {
    return open_ref;
}

void NvmeChunk::write_counter_add(uint64_t val) {
    write_counter.fetch_add(val, std::memory_order_relaxed);
}

uint64_t NvmeChunk::get_write_counter() const {
    return write_counter.load(std::memory_order_relaxed);
}

void NvmeChunk::read_counter_add(uint64_t val) {
    read_counter.fetch_add(val, std::memory_order_relaxed);
}

uint64_t NvmeChunk::get_read_counter() const {
    return read_counter.load(std::memory_order_relaxed);
}

uint64_t NvmeChunk::size() const {
    return chunk_size;
}

uint64_t NvmeChunk::used() const {
    return chunk_used;
}

uint32_t NvmeChunk::stat() const {
    return chunk_stat;
}

uint64_t NvmeChunk::vol_id() const {
    return volume_id;
}

uint32_t NvmeChunk::vol_index() const {
    return volume_index;
}

uint32_t NvmeChunk::spolicy() const {
    return chunk_spolicy;
}

bool NvmeChunk::is_preallocated() const {
    return flags & CHK_FLG_PREALLOC;
}

void NvmeChunk::set_blob(struct spdk_blob *blb) {
    blob = blb;
}

void NvmeChunk::set_blob_id(spdk_blob_id blobid) {
    blob_id = blobid;
}

spdk_blob_id NvmeChunk::get_blob_id() const {
    return blob_id;
}

struct spdk_blob *NvmeChunk::get_blob() const {
    return blob;
}

uint64_t NvmeChunk::get_chunk_id() const {
    return chk_id;
}

/*
 * load：load函数本身只从一个opened的blob中加载元数据信息到内存中，不执行open_blob操作
 * 所有的加载操作本质上就是将blob_xattr中的扩展属性变成元数据信息
*/
int NvmeChunk::load() {
    int ret = NVMECHUNK_LOAD_ERR;
    size_t value_len = 0;
    const char *name = nullptr;
    const void *value = nullptr;
    struct spdk_xattr_names *names;
    char data[BUFSIZE];

    size_t xattr_count = 0;
    uint64_t wr_count;
    uint64_t rd_count;

    spdk_blob_get_xattr_names(blob, &names);
    xattr_count = spdk_xattr_names_get_count(names);
    std::cout << "xattr_count = " << xattr_count << std::endl;

    for(size_t i = 0; i < xattr_count; i++) {
        name = spdk_xattr_names_get_name(names, i);
        spdk_blob_get_xattr_value(blob, name, &value, &value_len);
        std::cout << "| name = " << name << std::endl;

        if(value_len + 1 > sizeof(data)) {
            value_len = sizeof(data) - 1;
        }

        memcpy(&data, value, value_len);
        data[value_len] = '\0';
        std::cout << "| value = " << value << std::endl;

        //这边是否可以有更好的方法呢？
        if(strcmp(name, "chk_id") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &chk_id);
        } else if(strcmp(name, "vol_id") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &volume_id);
        } else if(strcmp(name, "index") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx32, &volume_index);
        } else if(strcmp(name, "stat") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx32, &chunk_stat);
        } else if(strcmp(name, "spolicy") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx32, &chunk_spolicy);
        } else if(strcmp(name, "flags") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx32, &flags);
        } else if(strcmp(name, "size") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &chunk_size);
        } else if(strcmp(name, "used") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &chunk_used);
        } else if(strcmp(name, "ctime") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &ctime);
        } else if(strcmp(name, "dst_id") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &dst_id);
        } else if(strcmp(name, "dst_ctime") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &dst_ctime);
        } else if(strcmp(name, "write_counter") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &wr_count);
            write_counter.store(wr_count, std::memory_order_relaxed);
        } else if(strcmp(name, "read_counter") == 0 && value_len != 0) {
            sscanf(data, "%" PRIx64, &rd_count);
            read_counter.store(rd_count, std::memory_order_relaxed);
        } else {
            
        }
    }

    spdk_xattr_names_free(names);
    return ret;
}
/*
 * store：store函数本身只会将chunk的元数据信息写回到blob的扩展属性中，并不会持久化扩展属性，所以不会调用sync_md
 * 如果想要提供持久化的元数据更新操作，则需要显式调用sync_md接口
*/
int NvmeChunk::store() {
    char key[BUFSIZE + 1];
    char value[BUFSIZE + 1];

    sprintf(key, "%s", "chk_id");
    sprintf(value, "%" PRIx64, chk_id);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store chunk_id failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "vol_id");
    sprintf(value, "%" PRIx64, volume_id);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store vol_id failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "index");
    sprintf(value, "%" PRIx32, volume_index);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store index failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "stat");
    sprintf(value, "%" PRIx32, chunk_stat);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store stat failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "spolicy");
    sprintf(value, "%" PRIx32, chunk_spolicy);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store spolicy failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "flags");
    sprintf(value, "%" PRIx32, flags);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store flags failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "size");
    sprintf(value, "%" PRIx64, chunk_size);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store size failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "used");
    sprintf(value, "%" PRIx64, chunk_used);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store used failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "ctime");
    sprintf(value, "%" PRIx64, ctime);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store ctime failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "dst_id");
    sprintf(value, "%" PRIx64, dst_id);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store dst_id failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "dst_ctime");
    sprintf(value, "%" PRIx64, dst_ctime);
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store dst_time failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "write_counter");
    sprintf(value, "%" PRIx64, write_counter.load(std::memory_order_relaxed));
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store write_counter failed.");
        return NVMECHUNK_STORE_ERR;
    }

    sprintf(key, "%s", "read_counter");
    sprintf(value, "%" PRIx64, read_counter.load(std::memory_order_relaxed));
    if(spdk_blob_set_xattr(blob, key, value, strlen(value)) != 0) {
        fct_->log()->lerror("store read counter failed.");
        return NVMECHUNK_STORE_ERR;
    }

    return NVMECHUNK_OP_SUCCESS;
}

uint32_t NvmeChunk::get_target_core(IOChannelType type) {
    int index = 0;
    size_t core_count = nvmestore->get_core_count(type);
    // index = chk_id % core_count; //这里对于1G的chunk，4KB访问相当于一直用单线程访问盘
    index = rand()%100;

    return nvmestore->get_io_core(type, index);
}

int NvmeChunk::read_sync(void *buff, uint64_t off, uint64_t len) {
    return NVMECHUNK_NO_OP;
}

int NvmeChunk::write_sync(void *buff, uint64_t off, uint64_t len) {
    return NVMECHUNK_NO_OP;
}

uint64_t NvmeChunk::length_to_page(uint64_t len) {
    uint64_t page_nums = 0;
    page_nums = len / (nvmestore->get_page_size());
    return page_nums;
}

uint64_t NvmeChunk::offset_to_page(uint64_t offset) {
    uint64_t page_nums = 0;
    page_nums = offset / (nvmestore->get_page_size());
    return page_nums;
}

int NvmeChunk::set_xattr(const std::string& name, const std::string& value) {
    size_t value_len = value.size();
    if(spdk_blob_set_xattr(blob, name.c_str(), value.c_str(), value_len) != 0) {
        fct_->log()->lerror("set xattr %s failed.", name.c_str());
        return NVMECHUNK_SETXATTR_ERR;
    }

    return NVMECHUNK_OP_SUCCESS;
}

int NvmeChunk::get_xattr(const std::string& name, std::string& value) {
    const void *val;
    char data[BUFSIZE + 1];
    size_t value_len;

    if(spdk_blob_get_xattr_value(blob, name.c_str(), &val, &value_len) != 0) {
        fct_->log()->lerror("get xattr %s failed.", name.c_str());
        return NVMECHUNK_GETXATTR_ERR;
    }
    memcpy(&data, val, value_len);
    data[value_len] = '\0';

    value = data;
    return NVMECHUNK_OP_SUCCESS;
}

void NvmeChunk::chunk_io_cb(void *cb_arg, int bserrno) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)cb_arg;

    NvmeChunk *chunk = oparg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    uint32_t curr_core = spdk_env_get_current_core();
    struct nvmestore_io_channel * nv_channel = nullptr;

    int opcode = oparg->opcode;
    switch(opcode) {
        case CHUNK_READ:
        {
            nv_channel = nvmestore->get_read_channels()->get_nv_channel(curr_core);
            if(bserrno)
                chunk->fct_->log()->lerror("read chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);

            chunk->read_counter_add(1);
            nv_channel->curr_io_ops_sub(1);
            nvmestore->get_read_channels()->num_io_ops_add(1);

            chunk->fct_->log()->ldebug("read completed..");

            struct chunk_read_arg *crarg = dynamic_cast<chunk_read_arg *>(oparg);
            if(crarg->rd_cb)
                crarg->rd_cb(crarg->cb_arg);

            if(crarg)
                delete crarg;
        }
        break;
        case CHUNK_WRITE: 
        {
            nv_channel = nvmestore->get_write_channels()->get_nv_channel(curr_core);

            if(bserrno)
                chunk->fct_->log()->lerror("write chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);

            chunk->write_counter_add(1);
            nv_channel->curr_io_ops_sub(1);
            nvmestore->get_read_channels()->num_io_ops_add(1);
            
            chunk->fct_->log()->ldebug("write completed..");

            struct chunk_write_arg * cwarg = dynamic_cast<chunk_write_arg *>(oparg);
            if(cwarg->wr_cb)
                cwarg->wr_cb(cwarg->cb_arg);
                
            if(cwarg)
                delete cwarg;
        }
        break;
    }
}

void NvmeChunk::chunk_io_start(void *arg1, void *arg2) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)arg1;
    NvmeChunk *chunk = oparg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    uint32_t curr_core = spdk_env_get_current_core();
    struct nvmestore_io_channel *nv_channel = nullptr;
    struct spdk_io_channel *io_channel = nullptr;

    uint64_t io_length = 0;
    uint64_t io_offset = 0;

    int opcode = oparg->opcode;

    chunk->fct_->log()->ldebug("curr_core = %ld", curr_core);

    switch(opcode) {
        case CHUNK_READ:
        {
            nv_channel = nvmestore->get_read_channels()->get_nv_channel(curr_core);
            io_channel = nv_channel->get_channel();
            nv_channel->curr_io_ops_add(1);

            struct chunk_read_arg *crarg = dynamic_cast<chunk_read_arg *>(oparg);
            io_length = chunk->length_to_page(crarg->length);
            io_offset = chunk->offset_to_page(crarg->offset);

            chunk->fct_->log()->ldebug("io_length = %ld", io_length);
            chunk->fct_->log()->ldebug("io_offset = %ld", io_offset);

            spdk_blob_io_read(chunk->get_blob(), io_channel, crarg->buff, io_offset, io_length, chunk_io_cb, crarg);
        }
        break;
        case CHUNK_WRITE:
        {
            nv_channel = nvmestore->get_write_channels()->get_nv_channel(curr_core);
            io_channel = nv_channel->get_channel();
            nv_channel->curr_io_ops_add(1);

            struct chunk_write_arg *cwarg = dynamic_cast<chunk_write_arg *>(oparg);
            io_length = chunk->length_to_page(cwarg->length);
            io_offset = chunk->offset_to_page(cwarg->offset);

            chunk->fct_->log()->ldebug("io_length = %ld", io_length);
            chunk->fct_->log()->ldebug("io_offset = %ld", io_offset);

            spdk_blob_io_write(chunk->get_blob(), io_channel, cwarg->buff, io_offset, io_length, chunk_io_cb, cwarg);
        }
        break;
    }
}

/*
 * read_async: read_async函数用于异步读取chunk数据
 * buffer: 读取缓冲区
 * len: 读取长度，以字节为单位
 * off: 偏移地址，以字节为单位
 * cb: callback回调函数
 * cb_arg: cb_arg回调函数参数
*/
int NvmeChunk::read_async(void *buff, uint64_t off, uint64_t len, chunk_opt_cb_t cb, void *cb_arg) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_read_arg *crarg = new chunk_read_arg(this, &ret, false, nullptr, 
                                                        off, len, buff, cb, cb_arg);

    uint32_t target_core = this->get_target_core(READ_CHANNEL);
    // std::cout << "target_core = " << target_core << std::endl;
    struct spdk_event *event = spdk_event_allocate(target_core, chunk_io_start, crarg, nullptr);
    spdk_event_call(event);

    return ret;
}

/*
 * write_async: write_async函数用于异步写入chunk数据
 * buffer: 写入缓冲区
 * len: 写入长度， 以字节为单位
 * off: 偏移地址， 以字节为单位
 * cb: callback回调函数
 * cb_arg: cb_arg表示回调函数参数
*/
int NvmeChunk::write_async(void *buff, uint64_t off, uint64_t len, chunk_opt_cb_t cb, void *cb_arg) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_write_arg *cwarg = new chunk_write_arg(this, &ret, false, nullptr, 
                                                        off, len, buff, cb, cb_arg);

    uint32_t target_core = this->get_target_core(WRITE_CHANNEL);
    // std::cout << "target_core = " << target_core << std::endl;
    struct spdk_event *event = spdk_event_allocate(target_core, chunk_io_start, cwarg, nullptr);
    spdk_event_call(event);

    return ret;
}

void NvmeChunk::delete_blob_cb(void *cb_arg, int bserrno) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)cb_arg;
    NvmeChunk *chunk = oparg->chunk;

    int opcode = oparg->opcode;

    switch(opcode) {
        case CHUNK_CREATE:
        {
            if(bserrno)
                chunk->fct_->log()->lerror("delete blob <%" PRIx64 "> failed: %d", chunk->get_blob_id(), bserrno);
            if(oparg->sync) {
                chunk->signal_create_completed();
            } else {
                //执行回调函数，并释放参数指针
                oparg->cb_fn(oparg, bserrno);
                if(oparg)
                    delete oparg;
            }
        }
        break;
        case CHUNK_REMOVE:
        {
            if(bserrno) {
                chunk->fct_->log()->lerror("delete blob <%" PRIx64 "> failed: %d", chunk->get_blob_id(), bserrno);
                if(sync)
                    *(oparg->ret) = NVMECHUNK_REMOVE_ERR; 
            }
            
            if(oparg->sync) {
                chunk->signal_create_completed();
            } else {
                oparg->cb_fn(oparg, bserrno);
                if(oparg)
                    delete oparg;
            }
        }
        break;
    }

    return ;
}

void NvmeChunk::close_blob_cb(void *cb_arg, int bserrno) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)cb_arg;
    NvmeChunk *chunk = oparg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    int opcode = oparg->opcode;
    switch(opcode) {
        case CHUNK_CREATE:
            if(sync) {
                switch(*(oparg->ret)) {
                    //如果是resize错误或者是sync错误，则删除blob
                    case NVMECHUNK_RESIZE_ERR:
                    case NVMECHUNK_SYNC_MD_ERR:
                        spdk_bs_delete_blob(nvmestore->get_blobstore(), chunk->get_blob_id(), delete_blob_cb, cb_arg);
                        break;
                    default:
                    //如果是close错误，则记录并同时创建成功，一般而言close操作很少出现错误
                        if(bserrno) {
                            chunk->fct_->log()->lerror("close chunk<%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                            *(oparg->ret) = NVMECHUNK_CLOSE_BLOB_ERR;
                        }
                        chunk->signal_create_completed();
                        break;
                }
            } else {
                if(bserrno) {
                    //如果关闭出现了错误
                    chunk->fct_->log()->lerror("close chunk<%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                }
                //调用完成回调函数
                oparg->cb_fn(oparg, bserrno);

                if(oparg)
                    delete oparg;
            }
        break;
        case CHUNK_CLOSE:
            if(bserrno) {
                chunk->fct_->log()->lerror("close chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                if(oparg->sync) {
                    *(oparg->ret) = NVMECHUNK_CLOSE_BLOB_ERR;
                    chunk->signal_close_completed();
                    return ;
                } else {
                    if(oparg->cb_fn)
                        oparg->cb_fn(oparg, bserrno);
                    if(oparg)
                        delete oparg;
                }
            }
            //关闭chunk之后，从chunk_map中删除该chunk实例
            nvmestore->get_chunk_map()->remove_chunk(chunk->get_chunk_id());
            nvmestore->get_fct()->log()->linfo("close blob completed");
            if(oparg->sync) {
                chunk->signal_close_completed();
            } else {
                if(oparg->cb_fn)
                    oparg->cb_fn(oparg, bserrno);

                if(oparg)
                    delete oparg;
            }
        break;
    }
    return ;
}

void NvmeChunk::sync_md_cb(void *cb_arg, int bserrno) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)cb_arg;
    NvmeChunk *chunk = oparg->chunk;

    int opcode = oparg->opcode;
    
    switch(opcode) {
        case CHUNK_CREATE:
            if(bserrno) {
                chunk->fct_->log()->lerror("sync chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                if(oparg->sync)
                    *(oparg->ret) = NVMECHUNK_SYNC_MD_ERR;

                spdk_blob_close(chunk->get_blob(), close_blob_cb, cb_arg);
                return ;
            }
            std::cout << "sync_md completed.." << std::endl;
            std::cout << "close blob start..." << std::endl;
            spdk_blob_close(chunk->get_blob(), close_blob_cb, cb_arg);
            break;
        case CHUNK_PERSIST_MD:
            if(bserrno) {
                chunk->fct_->log()->lerror("persist chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                if(oparg->sync)
                    *(oparg->ret) = NVMECHUNK_PERSIST_MD_ERR;
            }

            if(oparg->sync) {
                chunk->signal_persist_completed();
            } else {
                if(oparg->cb_fn)
                    oparg->cb_fn(oparg, bserrno);

                if(oparg)
                    delete oparg;
            }
            break;
        case CHUNK_CLOSE:
            if(bserrno) {
                chunk->fct_->log()->lerror("sync chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
                if(oparg->sync) {
                    *(oparg->ret) = NVMECHUNK_SYNC_MD_ERR;
                    chunk->signal_close_completed();
                    return ;
                } else {
                    if(oparg->cb_fn)
                        oparg->cb_fn(oparg, bserrno);

                    if(oparg)
                        delete oparg;
                }
            }
            chunk->reset_dirty();
            spdk_blob_close(chunk->get_blob(), close_blob_cb, cb_arg);
            break;
    }
}

void NvmeChunk::resize_blob_cb(void *cb_arg, int bserrno) {
    struct chunk_create_arg *ccarg = (struct chunk_create_arg *)cb_arg;
    NvmeChunk *chunk = ccarg->chunk;

    if(bserrno) {
        chunk->fct_->log()->lerror("resize chunk <%" PRIx64 "> failed: %d", chunk->get_chunk_id(), bserrno);
        *(ccarg->ret) = NVMECHUNK_RESIZE_ERR;
        spdk_blob_close(chunk->get_blob(), close_blob_cb, ccarg);
        return ;
    }

    chunk->store();
    spdk_blob_sync_md(chunk->get_blob(), sync_md_cb, ccarg);
}

void NvmeChunk::open_blob_cb(void *cb_arg, struct spdk_blob* blb, int bserrno) {
    NvmeChunkOpArg *oparg = (NvmeChunkOpArg *)cb_arg;
    NvmeChunk *chunk = oparg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    int opcode = oparg->opcode;

    switch(opcode) {
        case CHUNK_CREATE:
        {
            if(bserrno) {
                chunk->fct_->log()->lerror("open blob <%" PRIx64 "> failed: %d", chunk->get_blob_id(), bserrno);
                if(oparg->sync)
                    *(oparg->ret) = NVMECHUNK_OPEN_BLOB_ERR;

                spdk_bs_delete_blob(nvmestore->get_blobstore(), chunk->get_blob_id(), delete_blob_cb, cb_arg);          
            }

            std::cout << "open blob completed.." << std::endl;
            struct chunk_create_arg *ccarg = dynamic_cast<chunk_create_arg *>(oparg);
            uint64_t num_clusters = ccarg->opts->size / nvmestore->get_cluster_size();
            chunk->set_blob(blb);
            if(ccarg->opts->is_prealloc()) {
                std::cout << "chunk store start..." << std::endl;
                chunk->store();
                std::cout << "chunk store completed..." << std::endl;
                std::cout << "sync md start.." << std::endl;
                spdk_blob_sync_md(blb, sync_md_cb, ccarg);
            } else {
                spdk_blob_resize(blb, num_clusters, resize_blob_cb, ccarg);
            }
        }
        break;
        case CHUNK_OPEN:
        {
            if(bserrno) {
                chunk->fct_->log()->lerror("open blob <%" PRIx64 "> failed: %d", chunk->get_blob_id(), bserrno);
                if(oparg->sync) {
                    *(oparg->ret) = NVMECHUNK_OPEN_BLOB_ERR;
                    chunk->signal_open_completed();
                    return ;
                } else {
                    oparg->cb_fn(oparg, bserrno);
                    if(oparg)
                        delete oparg;
                }
            }
            std::cout << "blob open completed.." << std::endl;

            chunk->set_blob(blb);
            chunk->load();
            nvmestore->get_chunk_map()->insert_chunk(chunk);

            if(oparg->sync) {
                chunk->signal_open_completed();    
            } else {
                oparg->cb_fn(oparg, bserrno);

                if(oparg)
                    delete oparg;
            }
        }
        break;
    }

    return ;   
}

void NvmeChunk::chunk_create_cb(void *cb_arg, spdk_blob_id blobid, int bserrno) {
    struct chunk_create_arg *ccarg = (struct chunk_create_arg *)cb_arg;
    NvmeChunk *chunk = ccarg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    if(bserrno) {
        chunk->fct_->log()->lerror("create blob failed: %d", bserrno);
        if(ccarg->sync) {
            *(ccarg->ret) = NVMECHUNK_CREATE_BLOB_ERR;
            chunk->signal_create_completed();
        } else {
            ccarg->cb_fn(ccarg, bserrno);

            if(ccarg)
                delete ccarg;
        }
        return ;
    }

    std::cout << "create blob success.." << std::endl;
    std::cout << "open blob start..." << std::endl;
    chunk->set_blob_id(blobid);
    spdk_bs_open_blob(nvmestore->get_blobstore(), blobid, open_blob_cb, ccarg);

    return ;
}

void NvmeChunk::create_chunk_start(void *arg1, void *arg2) {
    struct chunk_create_arg *ccarg = (struct chunk_create_arg *)arg1;
    NvmeChunk *chunk = ccarg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    if(ccarg->opts->is_prealloc()) {
        struct spdk_blob_opts blob_opts;
        spdk_blob_opts_init(&blob_opts);
        blob_opts.num_clusters = ccarg->opts->size / nvmestore->get_cluster_size();
        blob_opts.thin_provision = true;

        std::cout << "num_clusters: " << blob_opts.num_clusters << std::endl;
        std::cout << "thin_provision: " << blob_opts.thin_provision << std::endl;

        std::cout << "create blob start.." << std::endl;
        spdk_bs_create_blob_ext(nvmestore->get_blobstore(), &blob_opts, chunk_create_cb, ccarg);
    } else {
        spdk_bs_create_blob(nvmestore->get_blobstore(), chunk_create_cb, ccarg);
    }

    return ;
}

/*
 * create: 该函数用于创建一个chunk
 * opts：创建的chunk的基本信息
 * sync：同步操作标识，如果是true，则表示同步操作，如果是false，则表示异步操作
 * cb_fn：异步操作回调函数，如果是同步操作，该值应该被置为nullptr.
 * 
*/
int NvmeChunk::create(const chunk_create_opts_t& opts, bool sync, CHUNKOP_CALLBACK cb_fn) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_create_arg *ccarg = new chunk_create_arg(this, &ret, &opts, sync, cb_fn);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), create_chunk_start, ccarg, nullptr);
    spdk_event_call(event);

    if(sync) {
        wait_create_completed();
        ctime = time(nullptr);

        if(ccarg)
            delete ccarg;
    }

    return ret;
}

void NvmeChunk::persist_md_start(void *arg1, void *arg2) {
    struct persist_arg *parg = (struct persist_arg *)arg1;
    NvmeChunk *chunk = parg->chunk;

    spdk_blob_sync_md(chunk->get_blob(), sync_md_cb, parg);
}

/*
 * persist_md：持久化chunk的元数据信息
 * sync: 同步标识，true标识是同步操作，false标识异步操作
 * cb_fn: 异步的回调函数，如果是同步操作，则该参数应该置为nullptr，即使置为其他参数也不会被调用
*/
int NvmeChunk::persist_md(bool sync, CHUNKOP_CALLBACK cb_fn) {
    int ret = NVMECHUNK_OP_SUCCESS;

    //首先判断是否调用过store，如果没有则需要调用一次store
    if(is_dirty())
        this->store();

    struct persist_arg *parg = new persist_arg(this, &ret, sync, cb_fn);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), persist_md_start, parg, nullptr);
    spdk_event_call(event);

    if(sync) {
        wait_persist_completed();

        if(parg)
            delete parg;
    }

    return ret;    
}

void NvmeChunk::chunk_remove_start(void *arg1, void *arg2) {
    struct chunk_remove_arg *crarg = (struct chunk_remove_arg *)arg1;

    NvmeChunk *chunk = crarg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    spdk_bs_delete_blob(nvmestore->get_blobstore(), crarg->blobid, delete_blob_cb, crarg);
}

/*
 * note: 该函数目前还没有使用场景，即不会被调用
 * remove: remove函数主要用于删除一个chunk，该函数用于删除一个chunk中指定的blobid,目前还没有用途
 * blobid：用于标识删除的blobid
 * sync： 同步表示，true表示同步，false表示false
 * cb_fn：完成回调函数
*/
int NvmeChunk::remove(spdk_blob_id blobid, bool sync, CHUNKOP_CALLBACK cb_fn) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_remove_arg *crarg = new chunk_remove_arg(this, &ret, blobid, sync, cb_fn);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), chunk_remove_start, crarg, nullptr);
    spdk_event_call(event);

    if(sync) {
        wait_remove_completed();

        if(crarg)
            delete crarg;
    }

    return ret;
}

void NvmeChunk::chunk_open_start(void *arg1, void *arg2) {
    struct chunk_open_arg *coarg = (struct chunk_open_arg *)arg1;
    NvmeChunk *chunk = coarg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    chunk->set_blob_id(coarg->blobid);
    std::cout << "open blob start...." << std::endl;
    spdk_bs_open_blob(nvmestore->get_blobstore(), coarg->blobid, open_blob_cb, coarg);
}

/*
 * open: open函数主要用于open一个chunk，将chunk的元数据信息加载到内存中
 * blobid: blobid标识要open的blob
 * sync: sync标识同步标志，如果是true，表示同步操作，如果是false，表示异步操作
 * cb_fn: 完成回调函数
*/

int NvmeChunk::open(spdk_blob_id blobid, bool sync, CHUNKOP_CALLBACK cb_fn) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_open_arg *coarg = new chunk_open_arg(this, &ret, blobid, sync, cb_fn);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), chunk_open_start, coarg, nullptr);
    spdk_event_call(event);

    if(sync) {
        wait_open_completed();

        if(coarg)
            delete coarg;
    }

    return ret;
}

void NvmeChunk::chunk_close_start(void *arg1, void *arg2) {
    struct chunk_close_arg *clarg = (struct chunk_close_arg *)arg1;
    NvmeChunk *chunk = clarg->chunk;
    NvmeStore *nvmestore = chunk->nvmestore;

    nvmestore->get_fct()->log()->linfo("chunk close start");
    if(chunk->is_dirty()) {
        chunk->store();
        //spdk_blob_sync_md(chunk->get_blob(), sync_md_cb, clarg);
        spdk_blob_sync_md(clarg->blb, sync_md_cb, clarg);
    } else {
        //spdk_blob_close(chunk->get_blob(), close_blob_cb, clarg);
        spdk_blob_close(clarg->blb, close_blob_cb, clarg);
    }
}

int NvmeChunk::close(struct spdk_blob *blb, bool sync, CHUNKOP_CALLBACK cb_fn) {
    int ret = NVMECHUNK_OP_SUCCESS;

    struct chunk_close_arg *clarg = new chunk_close_arg(this, &ret, blb, sync, cb_fn);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), chunk_close_start, clarg, nullptr);
    spdk_event_call(event);

    if(sync) {
        wait_close_completed();
        if(clarg)
            delete clarg;
    }

    return ret;
}