/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:06:40
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-23 15:24:10
 */
#include "chunkstore/nvmestore/chunkblobmap.h"

#include <math.h>

#include "util/utime.h"
#include "chunkstore/log_cs.h"

using namespace flame;

void ChunkBlobMap::set_blob_id(spdk_blob_id blobid) {
    map_blob_id = blobid;
}

spdk_blob_id ChunkBlobMap::get_chunk_map_blob_id() const {
    return map_blob_id;
}

spdk_blob_id ChunkBlobMap::get_blob_id(uint64_t chk_id) {
    std::map<uint64_t, spdk_blob_id>::iterator iter;
    for(iter = chunk_blob_map.begin(); iter != chunk_blob_map.end(); iter++) {
        if(iter->first == chk_id)
            return iter->second;
    }

    return 0;
}

void ChunkBlobMap::set_blob(struct spdk_blob *blb) {
    map_blob = blb;
}

bool ChunkBlobMap::is_dirty() {
    return dirty;
}

void ChunkBlobMap::cleanup() {
    chunk_blob_map.erase(chunk_blob_map.begin(), chunk_blob_map.end());
}

void ChunkBlobMap::insert_entry(uint64_t chk_id, spdk_blob_id blobid) {
    chunk_blob_map.emplace(chk_id, blobid);
    dirty = true;
}

void ChunkBlobMap::remove_entry(uint64_t chk_id) {
    std::map<uint64_t, spdk_blob_id>::iterator iter;
    iter = chunk_blob_map.find(chk_id);
    if(iter != chunk_blob_map.end()) {
        chunk_blob_map.erase(iter);
    }

    dirty = true;
}

bool ChunkBlobMap::chunk_exist(uint64_t chk_id) {
    std::map<uint64_t, spdk_blob_id>::iterator iter;
    iter = chunk_blob_map.find(chk_id);
    if(iter != chunk_blob_map.end())
        return true;

    return false;
}

uint64_t ChunkBlobMap::get_entry_nums() {
    return entry_nums;
}

void ChunkBlobMap::read_next_page_cb(void *cb_arg, int bserrno) {
    struct load_map_arg *larg = (struct load_map_arg *)cb_arg;
    NvmeStore *nvmestore = larg->nvmestore;
    ChunkBlobMap *chunk_blob_map = larg->chunk_blob_map;

    if(bserrno) {
        *(larg->ret) = READ_BLOB_ERR;
        chunk_blob_map->signal_load_completed();
        return ;
    }

    uint64_t chunk_id;
    spdk_blob_id blob_id;

    struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
    uint8_t *buffer = (uint8_t *)(larg->buffer);
    uint64_t page_size = nvmestore->get_page_size();

    size_t entries_in_page = page_size / (sizeof(spdk_blob_id) + sizeof(uint64_t));
    size_t rest_entries = chunk_blob_map->entry_nums - chunk_blob_map->loaded_entries;
    size_t real_entries = (rest_entries > entries_in_page ? entries_in_page : rest_entries);

    for(size_t i = 0; i < real_entries; i++) {
        memcpy(&chunk_id, buffer, sizeof(uint64_t));
        buffer += sizeof(uint64_t);

        memcpy(&blob_id, buffer, sizeof(spdk_blob_id));
        buffer += sizeof(spdk_blob_id);

        chunk_blob_map->insert_entry(chunk_id, blob_id);
        chunk_blob_map->loaded_entries++;
    }

    uint64_t offset = (chunk_blob_map->loaded_entries) / entries_in_page + 1;
    if(rest_entries > entries_in_page) {
        memset(buffer, 0, page_size);
        uint64_t length = 1;
        spdk_blob_io_read(chunk_blob_map->map_blob, io_channel, 
                            buffer, offset, length, read_next_page_cb, larg);
    } else {
        chunk_blob_map->signal_load_completed();
        return ;
    }
}

void ChunkBlobMap::read_map_blob_cb(void *cb_arg, int bserrno) {
    struct load_map_arg *larg = (struct load_map_arg *)cb_arg;
    NvmeStore *nvmestore = larg->nvmestore;
    ChunkBlobMap *chunk_blob_map = larg->chunk_blob_map;

    if(bserrno) {
        *(larg->ret) = READ_BLOB_ERR;
        chunk_blob_map->signal_load_completed();
        return ;
    }

    uint64_t chunk_id;
    spdk_blob_id blob_id;

    struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
    uint8_t *buffer = (uint8_t *)(larg->buffer);
    uint64_t page_size = nvmestore->get_page_size();

    struct chunk_blob_entry_descriptor descriptor;
    memcpy(&(descriptor), buffer, sizeof(uint64_t));
    buffer += sizeof(descriptor);

    chunk_blob_map->entry_nums = descriptor.entry_nums_total;

    size_t entries_in_page = (page_size - sizeof(struct chunk_blob_entry_descriptor)) / (sizeof(spdk_blob_id) + sizeof(uint64_t));
    size_t real_entries = (chunk_blob_map->entry_nums > entries_in_page ? entries_in_page : chunk_blob_map->entry_nums);

    for(size_t i = 0; i < real_entries; i++) {
        memcpy(&chunk_id, buffer, sizeof(uint64_t));
        buffer += sizeof(uint64_t);
        memcpy(&blob_id, buffer, sizeof(spdk_blob_id));
        buffer += sizeof(spdk_blob_id);

        chunk_blob_map->insert_entry(chunk_id, blob_id);
        chunk_blob_map->loaded_entries++;
    }

    buffer = (uint8_t *)(larg->buffer);
    if(chunk_blob_map->entry_nums > entries_in_page) {
        memset(buffer, 0, page_size);
        uint64_t length = 1;
        spdk_blob_io_read(chunk_blob_map->map_blob, io_channel, buffer, 
                        1, length,
                        read_next_page_cb, larg);
    } else {
        chunk_blob_map->signal_load_completed();
        return ;
    }
}

void ChunkBlobMap::read_map_blob(void *arg1, void *arg2) {
    struct load_map_arg *larg = (struct load_map_arg *)arg1;
    NvmeStore *nvmestore = larg->nvmestore;
    ChunkBlobMap *chunk_blob_map = larg->chunk_blob_map;

    struct spdk_io_channel *io_channel = nvmestore->get_io_channel(WRITE_CHANNEL, 0);
    uint64_t length = 1;
    spdk_blob_io_read(chunk_blob_map->map_blob, io_channel, 
                    larg->buffer, 0, length, 
                    read_map_blob_cb, larg);
}

void ChunkBlobMap::delete_blob_cb(void *cb_arg, int bserrno) {
    ChunkBlobMapOpArg *oparg = (ChunkBlobMapOpArg *)cb_arg;

    NvmeStore *nvmestore = oparg->nvmestore;
    ChunkBlobMap *chunk_blob_map = oparg->chunk_blob_map;

    switch(oparg->opcode) {
        case CREATE:
            chunk_blob_map->signal_create_completed();
            break;
    }

    return ;
}

void ChunkBlobMap::open_map_blob_cb(void *cb_arg, struct spdk_blob *blb, int bserrno) {
    ChunkBlobMapOpArg *oparg = (ChunkBlobMapOpArg *)cb_arg;

    NvmeStore *nvmestore = oparg->nvmestore;
    ChunkBlobMap *chunk_blob_map = oparg->chunk_blob_map;

    switch(oparg->opcode) {
        case CREATE:
        {
            if(bserrno) {
                *(oparg->ret) = CREATE_BLOB_ERR;
                spdk_bs_delete_blob(nvmestore->get_blobstore(), chunk_blob_map->map_blob_id, delete_blob_cb, cb_arg);
                //chunk_blob_map->signal_create_completed();
                return ;
            }

            chunk_blob_map->set_blob(blb);
            std::cout << "open map_blob success." << std::endl;
            //这里由于不会产生元数据和数据的冲突操作，所以，在此我们直接采用在meta_core进行一次临时IO操作
            struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
            uint64_t length = 1;
            std::cout << "write_init_info start..." << std::endl;

            struct create_map_arg *cmarg = dynamic_cast<struct create_map_arg *>(oparg);
            spdk_blob_io_write(chunk_blob_map->map_blob, io_channel, cmarg->buffer, 
                                0, length, write_init_info_cb, cmarg);
        }
        break;   
        case LOAD:
        {
            if(bserrno) {
                *(oparg->ret) = LOAD_BLOB_ERR;
                chunk_blob_map->signal_load_completed();
                return ;
            }

            struct load_map_arg *larg = dynamic_cast<struct load_map_arg *>(oparg);
            chunk_blob_map->set_blob(blb);
            struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
            if(io_channel == nullptr){
                io_channel = spdk_bs_alloc_io_channel(nvmestore->get_blobstore()); 
                nvmestore->set_meta_channel(io_channel);
                io_channel = nvmestore->get_meta_channel();
            }  
            uint64_t length = 1;
            spdk_blob_io_read(chunk_blob_map->map_blob, io_channel, larg->buffer, 
                                0, length, read_map_blob_cb, larg);
        }
        break;
    }
}

void ChunkBlobMap::load_map_start(void *arg1, void *arg2) {
    struct load_map_arg *larg = (struct load_map_arg *)arg1;
    NvmeStore *nvmestore = larg->nvmestore;
    ChunkBlobMap *chunk_blob_map = larg->chunk_blob_map;

    spdk_bs_open_blob(nvmestore->get_blobstore(), chunk_blob_map->map_blob_id, open_map_blob_cb, larg);
}

/*
 * load()函数的主要工作是从blobstore中读取chunk和blob的映射关系
 * 1. load函数会阻塞线程，直到完成为之，为此必须在admin_core所在的线程中调用，否则就会阻塞meta_core和io_core,从而导致死锁
 * 2. ChunkBlobMap在load的时候，采用的是一页一页的读取的方法, 这主要是因为在loaded之前是不知道chunk_blob_map中的条目数的，所以一次分配多少的内存是不得而知的
*/

int ChunkBlobMap::load() {
    int ret = 0;
    uint64_t page_size = nvmestore->get_page_size();
    void *buff = spdk_dma_malloc(page_size, 4096, NULL);
    if(buff == nullptr)
        return -1;

    struct load_map_arg *larg = new load_map_arg(this, nvmestore, &ret, buff);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), load_map_start, larg, NULL);
    spdk_event_call(event);

    //在此等待load完成  
    wait_load_completed();

    if(buff)
        spdk_dma_free(buff);

    if(larg)
        delete larg;

    return ret;
}


void ChunkBlobMap::store_cb(void *cb_arg, int bserrno) {
    struct store_map_arg * sarg = (struct store_map_arg *)cb_arg;
    ChunkBlobMap *chunk_blob_map = sarg->chunk_blob_map;

    if(bserrno) {
        *(sarg->ret) = WRITE_BLOB_ERR;
        std::cout << "store failed: " << bserrno << std::endl;
    }
    chunk_blob_map->signal_store_completed();
    chunk_blob_map->dirty = false;
    std::cout << "store success..." << std::endl;
    return ;
}

void ChunkBlobMap::store_map_start(void *arg1, void *arg2) {
    struct store_map_arg *sarg = (struct store_map_arg *)arg1;
    ChunkBlobMap *chunk_blob_map = sarg->chunk_blob_map;
    NvmeStore *nvmestore = sarg->nvmestore;
    struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
    uint64_t length = sarg->length;
    spdk_blob_io_write(chunk_blob_map->map_blob, io_channel, 
                        sarg->buff, 0, length, store_cb, sarg);
}

/* store()：该函数用于向blobstore中存储chunk和blob的映射关系
 * 1. 这个函数一般在admin_core中调用，因为他会阻塞meta_core和io_core，所以不要在meta_core和io_core所在的线程中调用
 * 2. ChunkBlobMap的store操作是，采用一次io_write就可以完成了, 这主要是因为store的时候，我们可以计算出需要多少个page页
*/
int ChunkBlobMap::store() {
    if(this->map_blob == nullptr) {
        return BLOB_CLOSED;
    }

    int ret = 0;

    uint64_t page_size = nvmestore->get_page_size();
    uint64_t page_count = ((entry_nums * (sizeof(uint64_t) + sizeof(spdk_blob_id))) + sizeof(struct chunk_blob_entry_descriptor)) / page_size + 1;
    uint64_t buffer_size = page_count * page_size;

#ifdef DEBUG
    std::cout << "entry_nums = " << entry_nums << std::endl;
    std::cout << "page_count = " << page_count << std::endl;
    std::cout << "buffer_size = " << buffer_size << std::endl;
#endif

    void *buff = spdk_dma_malloc(buffer_size, 4096, NULL);
    if(buff == nullptr) 
        return -1;

    uint64_t entries_in_page = (page_size - sizeof(struct chunk_blob_entry_descriptor)) / (sizeof(uint64_t) + sizeof(spdk_blob_id));
    struct chunk_blob_entry_descriptor descriptor;
    descriptor.entry_nums_total = entry_nums;

    uint8_t *buffer_ptr = (uint8_t *)buff;
    memcpy(buffer_ptr, &descriptor, sizeof(struct chunk_blob_entry_descriptor));
    buffer_ptr += sizeof(struct chunk_blob_entry_descriptor);

    std::map<uint64_t, spdk_blob_id>::iterator iter;
    for(iter = chunk_blob_map.begin(); iter != chunk_blob_map.end(); iter++) {

#ifdef DEBUG
        std::cout << "<" << iter->first << ", " << iter->second << ">" << std::endl;
#endif
        memcpy(buffer_ptr, &(iter->first), sizeof(uint64_t));
        buffer_ptr += sizeof(uint64_t);
        memcpy(buffer_ptr, &(iter->second), sizeof(spdk_blob_id));
        buffer_ptr += sizeof(spdk_blob_id);
    }
  
    uint64_t offset = (sizeof(struct chunk_blob_entry_descriptor) + entry_nums * (sizeof(uint64_t) + sizeof(spdk_blob_id)));
    memset(buffer_ptr, 0, buffer_size - offset);
    uint64_t length = ceil(buffer_size / this->nvmestore->get_page_size());
    struct store_map_arg *sarg = new store_map_arg(this, nvmestore, &ret, buff, length);

    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), store_map_start, sarg, NULL);
    spdk_event_call(event);

    wait_store_completed();

    if(sarg)
        delete sarg;
    
    //if(buff)
    //    spdk_dma_free(buff);

    return ret;
}

void ChunkBlobMap::write_init_info_cb(void *cb_arg, int bserrno) {
    struct create_map_arg *cmarg = (struct create_map_arg *)cb_arg;
    NvmeStore *nvmestore = cmarg->nvmestore;
    ChunkBlobMap *chunk_blob_map = cmarg->chunk_blob_map;

    if(bserrno) {
        *(cmarg->ret) = WRITE_BLOB_ERR;
        spdk_blob_close(chunk_blob_map->map_blob, close_blob_cb, cmarg);
        return ;
    }

    std::cout << "write init info success.." << std::endl;

    //由于所有的操作都会在metacore中完成，所以不需要跨core的操作
    spdk_blob_close(chunk_blob_map->map_blob, close_blob_cb, cb_arg);

    return ;
}

void ChunkBlobMap::write_init_info(void *arg1, void *arg2) {
    struct create_map_arg *cmarg = (struct create_map_arg *)arg1;
    NvmeStore *nvmestore = cmarg->nvmestore;
    ChunkBlobMap *chunk_blob_map = cmarg->chunk_blob_map;

    struct spdk_io_channel *io_channel = nvmestore->get_meta_channel();
    uint64_t length = 1;

    spdk_blob_io_write(chunk_blob_map->map_blob, io_channel, 
                        cmarg->buffer, 0, length, 
                        write_init_info_cb, cmarg);
}

void ChunkBlobMap::create_map_blob_cb(void *cb_arg, spdk_blob_id blobid, int bserrno) {
    struct create_map_arg *cmarg = (struct create_map_arg*)cb_arg;
    NvmeStore *nvmestore = cmarg->nvmestore;
    ChunkBlobMap *chunk_blob_map = cmarg->chunk_blob_map;

    if(bserrno) {
        *(cmarg->ret) = CREATE_BLOB_ERR;
        std::cout << "create map_blob cb error: " << bserrno << std::endl;
        chunk_blob_map->signal_create_completed();
        return ;    
    }

    chunk_blob_map->map_blob_id = blobid;
    spdk_bs_open_blob(nvmestore->get_blobstore(), blobid, open_map_blob_cb, cmarg);
}

void ChunkBlobMap::create_map_blob_start(void *arg1, void *arg2) {
    struct create_map_arg *cmarg = (struct create_map_arg *)arg1;
    NvmeStore *nvmestore = cmarg->nvmestore;

    spdk_bs_create_blob_ext(nvmestore->get_blobstore(), cmarg->opts, create_map_blob_cb, cmarg);
}

/*
 * create_blob()：该函数用于创建一个用于存储chunk和blob映射关系的blob
 * 1. 该函数会阻塞直到创建成功，所以必须在`非meta_core和非io_core`中调用，否则会造成永久阻塞
 * 2. 创建之后，向该blob中写入头部信息是必要的，否则在load的时候就会产生不可知的错误。
*/
int ChunkBlobMap::create_blob(struct spdk_blob_opts &opts) {
    int ret = 0;
    struct chunk_blob_entry_descriptor desc;
    desc.entry_nums_total = 0;
    desc.rest = 0;

    void *buffer = spdk_dma_malloc(nvmestore->get_page_size(), 4096, NULL);
    if(buffer == nullptr)
        return -ENOMEM;
    memcpy(buffer, &desc, sizeof(struct chunk_blob_entry_descriptor));

    struct create_map_arg *cmarg = new create_map_arg(this, nvmestore, &ret, &opts, buffer);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), create_map_blob_start, cmarg, nullptr);
    spdk_event_call(event);

    wait_create_completed();

    if(cmarg)
        delete cmarg;

    if(buffer)
        spdk_dma_free(buffer);

    return ret;
}

//最终会根据arg->opcode来决定，是哪个操作
void ChunkBlobMap::close_blob_cb(void *cb_arg, int bserrno) {
    ChunkBlobMapOpArg *oparg = (ChunkBlobMapOpArg *)cb_arg;
    ChunkBlobMap *chunk_blob_map = oparg->chunk_blob_map;
    NvmeStore *nvmestore = chunk_blob_map->nvmestore;

    chunk_blob_map->set_blob(nullptr);

    switch(oparg->opcode) {
        case CLOSE:
            if(bserrno) {
                *(oparg->ret) = CLOSE_BLOB_ERR;
                std::cout << "close blob failed: " << bserrno << std::endl;
            }
            chunk_blob_map->signal_close_completed();
            break;
        case CREATE:
            if(*(oparg->ret) == 0 || bserrno)   //如果是正常关闭或者是关闭失败，则直接通知创建成功
                chunk_blob_map->signal_create_completed();
            else if(*(oparg->ret) == WRITE_BLOB_ERR) //如果是由于写失败导致的关闭操作，还需要delete操作
                spdk_bs_delete_blob(nvmestore->get_blobstore(), chunk_blob_map->map_blob_id, delete_blob_cb, cb_arg);
            break;
    }

    return ;
}

void ChunkBlobMap::close_blob_start(void *arg1, void *arg2) {
    ChunkBlobMapOpArg *oparg = (ChunkBlobMapOpArg *)arg1;
    ChunkBlobMap *chunk_blob_map = oparg->chunk_blob_map;

    spdk_blob_close(chunk_blob_map->map_blob, close_blob_cb, arg1);
}

/*
 * close_blob(): 该函数会用于关闭一个打开的chunk_blob_map的blob
 * 1. 同样的，该函数会阻塞直到关闭成功，所以必须在admin_core中调用， 否则会造成永久阻塞
 * 2. 首先判断是否是dirty的，如果是，则store，进行持久化最新的值，之后关闭
*/
int ChunkBlobMap::close_blob() {
    int ret = 0;

    if(is_dirty()) {
        ret = this->store();
        if(ret != 0)
            return ret;
    }

    struct close_map_arg *carg = new close_map_arg(this, nvmestore, &ret);
    struct spdk_event *event = spdk_event_allocate(nvmestore->get_meta_core(), close_blob_start, carg, nullptr);
    spdk_event_call(event);

    wait_close_completed();

    if(carg)
        delete carg;

    return ret;
}