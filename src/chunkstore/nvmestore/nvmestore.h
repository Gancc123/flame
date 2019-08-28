/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-06-10 09:06:40
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-28 14:37:23
 */
#ifndef FLAME_CHUNKSTORE_NVME_H
#define FLAME_CHUNKSTORE_NVME_H

#include <cstdint>
#include <string>
#include <map>
#include <atomic>
#include <fstream>
#include <vector>

#include "chunkstore/chunkstore.h"
#include "chunkstore/nvmestore/nvmeconf.h"
#include "chunkstore/nvmestore/chunkblobmap.h"
#include "chunkstore/nvmestore/nvmechunkmap.h"

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/blob_bdev.h"
#include "spdk/blob.h"
#include "spdk/log.h"
#include "spdk/version.h"
#include "spdk/string.h"

#define BUFSIZE 1024

#define DEFAULT_MAP_BLOB_SIZE           4
#define DEFAULT_CHANNEL_NUMS            32

#define NVMESTORE_OP_SUCCESS            0x00
#define NVMESTORE_FORMAT_ERR            0x01
#define NVMESTORE_CREATE_MAPBLOB_ERR    0x02
#define NVMESTORE_MOUNT_ERR             0x03
#define NVMESTORE_ALLOC_CHANNEL_ERR     0x04
#define NVMESTORE_LOAD_BS_ERR           0x05
#define NVMESTORE_UNMOUNT_ERR           0x06
#define NVMESTORE_STORE_MAPBLOB_ERR     0x07
#define NVMESTORE_CLOSE_MAPBLOB_ERR     0x08
#define NVMESTORE_CHUNK_USING           0x09
#define NVMESTORE_CHUNK_EXISTED         0x0a
#define NVMESTORE_CHUNK_NOT_EXISTED     0x0b
#define NVMESTORE_CHUNK_CREATE_ERR      0x0c
#define NVMESTORE_CHUNK_DELETE_ERR      0x0d
#define NVMESTORE_CHUNK_CLOSE_ERR       0x0e

#define NVMECHUNK_OP_SUCCESS            0x00
#define NVMECHUNK_LOAD_ERR              0x01
#define NVMECHUNK_STORE_ERR             0x02
#define NVMECHUNK_NO_OP                 0x03
#define NVMECHUNK_SETXATTR_ERR          0x04
#define NVMECHUNK_GETXATTR_ERR          0x05
#define NVMECHUNK_REMOVE_ERR            0x06
#define NVMECHUNK_RESIZE_ERR            0x07
#define NVMECHUNK_SYNC_MD_ERR           0x08
#define NVMECHUNK_CLOSE_BLOB_ERR        0x09
#define NVMECHUNK_PERSIST_MD_ERR        0x0a
#define NVMECHUNK_OPEN_BLOB_ERR         0x0b
#define NVMECHUNK_CREATE_BLOB_ERR       0x0c

namespace flame {
class NvmeChunkMap;
class ChunkBlobMap;
class NvmeChunk;
class IOChannels;

class NvmeStore;
class NvmeChunk;

struct nvmestore_io_channel;
struct NvmeChunkOpArg;
struct NvmeStoreOpArg;

//定义一个回调函数指针，用于在异步调用成功之后执行
typedef void (*CHUNKOP_CALLBACK)(NvmeChunkOpArg *, int);

enum NvmeStoreState {
    NVMESTORE_STATE_NEW,
    NVMESTORE_STATE_FORMATING,
    NVMESTORE_STATE_FORMATED,
    NVMESTORE_STATE_MOUNTING,
    NVMESTORE_STATE_MOUNTED,
    NVMESTORE_STATE_UNMOUNTING,
    NVMESTORE_STATE_DOWN,
};

enum NvmeStoreOpType {
    FORMAT,
    MOUNT,
    UNMOUNT,
    CREATE_CHUNK,
    REMOVE_CHUNK,
    OPEN_CHUNK,
    CLOSE_CHUNK
};

enum NvmeChunkOpType {
    CHUNK_CREATE,
    CHUNK_REMOVE,
    CHUNK_OPEN,
    CHUNK_CLOSE,
    CHUNK_PERSIST_MD,
    CHUNK_READ,
    CHUNK_WRITE
};

enum NvmeIOChannelOpType {
    ALLOC_CHANNEL
};

enum IOChannelType {
    READ_CHANNEL,
    WRITE_CHANNEL
};

enum NvmeChunkState {
    NVMECHUNK_STATE_NEW,
    NVMECHUNK_STATE_OPENING,
    NVMECHUNK_STATE_CLOSING,
    NVMECHUNK_STATE_PERSITING,
    NVMECHUNK_STATE_ACTIVE,
    NVMECHUNK_STATE_CLOSED
};

struct nvmestore_io_channel {
    IOChannels *group;

    uint32_t lcore;
    std::atomic<uint64_t> curr_io_ops;

    struct spdk_io_channel *io_channel;
public:
    nvmestore_io_channel(IOChannels *_group, uint32_t _core): group(_group), 
                                        lcore(_core), curr_io_ops(0), io_channel(nullptr) {

    }

    struct spdk_io_channel *get_channel() {
        return io_channel;
    }

    uint32_t get_core() {
        return lcore;
    }

    void curr_io_ops_add(uint32_t val);
    void curr_io_ops_sub(uint32_t val);
    uint64_t get_curr_io_ops() const;
    void alloc(NvmeStore *_store);
    void free();
    static void alloc_io_channel(void *arg1, void *arg2);

    ~nvmestore_io_channel() {

    }
};

class IOChannels {
public:
    IOChannelType type;
    size_t channel_nums;
    std::vector<struct nvmestore_io_channel *> channels;
    std::atomic<uint64_t> num_io_ops;
    std::atomic<uint64_t> curr_io_ops;

    size_t alloc_nums;
    pthread_mutex_t alloc_mutex;
    pthread_cond_t alloc_cond;

public:
    IOChannels(IOChannelType _type, size_t _nums): type(_type), 
                            channel_nums(_nums), num_io_ops(0), 
                            curr_io_ops(0), alloc_nums(0) {
        pthread_mutex_init(&alloc_mutex, NULL);
        pthread_cond_init(&alloc_cond, NULL);
    }

    virtual ~IOChannels() {
        pthread_mutex_destroy(&alloc_mutex);
        pthread_cond_destroy(&alloc_cond);
    }

    void alloc_channels(NvmeStore *_store);
    void free_channels();
    void add_channel(struct nvmestore_io_channel *channel);
    void remove_channel(uint32_t lcore);
    void move_channel_to_other_group(struct nvmestore_io_channel *channel, IOChannels *group);

    void signal_alloc_channel_completed() {
        pthread_mutex_lock(&alloc_mutex);
        alloc_nums++;
        if(alloc_nums >= channel_nums)
            pthread_cond_signal(&alloc_cond);
        pthread_mutex_unlock(&alloc_mutex);
    }

    void wait_alloc_channel_completed() {
        pthread_mutex_lock(&alloc_mutex);
        while(alloc_nums < channel_nums) {
            pthread_cond_wait(&alloc_cond, &alloc_mutex);
        }
        pthread_mutex_unlock(&alloc_mutex);
    }

    void num_io_ops_add(uint64_t val) {
        num_io_ops.fetch_add(val, std::memory_order_relaxed);
    }

    void curr_io_ops_add(uint64_t val) {
        curr_io_ops.fetch_add(val, std::memory_order_relaxed);
    }

    void curr_io_ops_sub(uint64_t val) {
        curr_io_ops.fetch_sub(val, std::memory_order_relaxed);
    }

    uint64_t get_curr_io_ops() const {
        return curr_io_ops.load(std::memory_order_relaxed);
    }

    uint64_t get_num_io_ops() const {
        return num_io_ops.load(std::memory_order_relaxed);
    }

    struct spdk_io_channel *get_channel();
    struct spdk_io_channel *get_io_channel_by_index(size_t index);
    struct spdk_io_channel *get_io_channel_by_core(uint32_t core);
    struct nvmestore_io_channel *get_nv_channel(uint32_t core);

};

class NvmeStore : public ChunkStore {
private:
    //blobstore相关信息
    struct spdk_bdev            *bdev;
    struct spdk_bs_dev          *bs_dev;
    struct spdk_blob_store      *blobstore;

    /*
     * io channels
     * 将这里替换成读写分离的io_channels;
    */ 
    //std::vector<struct spdk_io_channel *> channles;
    
    //read_channels 用于读操作的channel group
    IOChannels *read_channels;
    //write_channels 用于写操作的channel group
    IOChannels *write_channels;

    struct spdk_io_channel *meta_channel;

    //配置文件信息, 包含了配置信息，包括core_mask等信息
    NvmeConf            config_file;

    std::string         dev_name;

    //base information
    uint64_t    store_id;
    std::string cluster_name;
    std::string name;

    uint64_t page_size;             //page_size: 表示nvmestore中的page_size,用于最小的原子操作单位, 以字节为单位
    uint64_t chunk_pages;           //chunk_pages: 表示每个chunk占用多少个page，在使用不等大小chunk之后，该字段被弃用
    uint64_t cluster_size;          //cluster_size: 表示nvmestore中的blobstore的cluster的大小，以字节为单位
    uint64_t total_chunks;          //total_chunks：表示该nvmestore中共有多少个chunk
    uint64_t used_size;             //used_size: nvmestore已被使用的空间大小，以字节为单位
    uint64_t format_time;           //format_time: 表示格式化的时间
    uint64_t data_cluster_count;    //data_cluster_count: 表示该nvmestore中data cluster的数量

    //nvmestore的状态
    NvmeStoreState state;
    // dirty标识
    bool dirty;

    //chunk_map用来保存open的chunk的信息
    NvmeChunkMap    *chunk_map;
    //chunk_blob_map用来保存chunk和blob的映射关系
    ChunkBlobMap    *chunk_blob_map;

    bool formated;
    pthread_mutex_t format_mutex;
    pthread_cond_t format_cond;

    //uint32_t create_nums;
    //pthread_mutex_t create_mutex;
    //pthread_cond_t create_cond;

    bool umounted;
    pthread_mutex_t umount_mutex;
    pthread_cond_t  umount_cond;

    bool mounted;
    pthread_mutex_t mount_mutex;
    pthread_cond_t  mount_cond;

    bool removed;
    pthread_mutex_t remove_mutex;
    pthread_cond_t  remove_cond;

    static void create_map_blob_cb(void *cb_arg, spdk_blob_id blobid, int bserrno);
    static void bs_init_cb(void *cb_arg, struct spdk_blob_store *bs, int bserrno);
    static void format_start(void *arg1, void *arg2);

    static void open_map_blob_cb(void *cb_arg, struct spdk_blob *blb, int bserrno);
    static void bs_load_cb(void *cb_arg, struct spdk_blob_store *bs, int bserrno);
    static void mount_start(void *arg1, void *arg2);

    static void chunk_create_cb(void *cb_arg, spdk_blob_id blobid, int bserrno);
    static void chunk_create_start(void *arg1, void *arg2);

    static void delete_blob_cb(void *cb_arg, int bserrno);
    static void remove_chunk_start(void *arg1, void *arg2);

    static void open_chunk_cb(void *cb_arg, struct spdk_blob *blb, int bserrno);
    static void open_chunk_start(void *arg1, void *arg2);

    static void store_unload_cb(void *cb_arg, int bserrno);
    static void unmount_start(void *arg1, void *arg2);

    NvmeStore(FlameContext *fct): ChunkStore(fct), bdev(nullptr), bs_dev(nullptr),
                                blobstore(nullptr), read_channels(nullptr), write_channels(nullptr), 
                                store_id(0), cluster_name(""), name(""), cluster_size(0), 
                                format_time(0), used_size(0), data_cluster_count(0),
                                meta_channel(nullptr), page_size(0), chunk_pages(0), total_chunks(0),
                                state(NVMESTORE_STATE_NEW), chunk_map(nullptr), chunk_blob_map(nullptr) {
        formated = false;
        pthread_mutex_init(&format_mutex, NULL);
        pthread_cond_init(&format_cond, NULL);

        mounted = false;
        pthread_mutex_init(&mount_mutex, NULL);
        pthread_cond_init(&mount_cond, NULL);

        umounted = false;
        pthread_mutex_init(&umount_mutex, NULL);
        pthread_cond_init(&umount_cond, NULL);

        removed = false;
        pthread_mutex_init(&remove_mutex, NULL);
        pthread_cond_init(&remove_cond, NULL);
    }

    NvmeStore(FlameContext *fct, 
            const std::string& _device_name, 
            const std::string& _info_file): ChunkStore(fct), bdev(nullptr), bs_dev(nullptr), blobstore(nullptr), 
                                            read_channels(nullptr), write_channels(nullptr), meta_channel(nullptr),
                                            store_id(0), cluster_name(""), name(""), cluster_size(0), 
                                            format_time(0), used_size(0), data_cluster_count(0),
                                            page_size(0), chunk_pages(0), total_chunks(0),
                                            state(NVMESTORE_STATE_NEW), chunk_map(nullptr), chunk_blob_map(nullptr),
                                            dev_name(_device_name), config_file(_info_file) {

        formated = false;
        pthread_mutex_init(&format_mutex, NULL);
        pthread_cond_init(&format_cond, NULL);

        mounted = false;
        pthread_mutex_init(&mount_mutex, NULL);
        pthread_cond_init(&mount_cond, NULL);

        umounted = false;
        pthread_mutex_init(&umount_mutex, NULL);
        pthread_cond_init(&umount_cond, NULL);

        removed = false;
        pthread_mutex_init(&remove_mutex, NULL);
        pthread_cond_init(&remove_cond, NULL);
    }

public:
    static NvmeStore *create_nvmestore(FlameContext* fct, const std::string& url);
    static NvmeStore *create_nvmestore(FlameContext* fct, const std::string& device_name, const std::string& info_file);

    virtual ~NvmeStore() {
        pthread_mutex_destroy(&format_mutex);
        pthread_mutex_destroy(&mount_mutex);
        pthread_mutex_destroy(&remove_mutex);
        pthread_mutex_destroy(&umount_mutex);

        pthread_cond_destroy(&format_cond);
        pthread_cond_destroy(&mount_cond);
        pthread_cond_destroy(&umount_cond);
        pthread_cond_destroy(&remove_cond);
    } 

    //同步format操作
    void signal_format_completed() {
        pthread_mutex_lock(&format_mutex);
        formated = true;
        pthread_cond_signal(&format_cond);
        pthread_mutex_unlock(&format_mutex);
    }

    void wait_format_completed() {
        pthread_mutex_lock(&format_mutex);
        while(formated == false) {
            pthread_cond_wait(&format_cond, &format_mutex);
        }
        pthread_mutex_unlock(&format_mutex);
    }

    void signal_remove_completed() {
        pthread_mutex_lock(&remove_mutex);
        removed = true;
        if(removed)
            pthread_cond_signal(&remove_cond);
        pthread_mutex_unlock(&remove_mutex);
    }

    void wait_remove_completed() {
        pthread_mutex_lock(&remove_mutex);
        while(!removed) {
            pthread_cond_wait(&remove_cond, &remove_mutex);
        }
        removed = false;
        pthread_mutex_unlock(&remove_mutex);
    }

    //同步mount操作
    void signal_mount_completed() {
        pthread_mutex_lock(&mount_mutex);
        mounted = true;
        pthread_cond_signal(&mount_cond);
        pthread_mutex_unlock(&mount_mutex);
    }

    void wait_mount_completed() {
        pthread_mutex_lock(&mount_mutex);
        while(mounted == false) {
            pthread_cond_wait(&mount_cond, &mount_mutex);
        }
        pthread_mutex_unlock(&mount_mutex);
    }

    //同步unmount操作
    void signal_unmount_completed() {
        pthread_mutex_lock(&umount_mutex);
        umounted = true;
        if(umounted)
            pthread_cond_signal(&umount_cond);
        pthread_mutex_unlock(&umount_mutex);
    }

    void wait_unmount_completed() {
        pthread_mutex_lock(&umount_mutex);
        while(!umounted)
            pthread_cond_wait(&umount_cond, &umount_mutex);
        pthread_mutex_unlock(&umount_mutex);
    }

    //调试辅助函数
    void print_store();

    int get_info(cs_info_t& info) const;
    int set_info(const cs_info_t& info);

    std::string get_device_name() const;
    std::string get_driver_name() const;
    std::string get_config_info() const;
    std::string get_runtime_info() const;

    int get_io_mode() const;
    struct spdk_bdev    *get_bdev();
    struct spdk_bs_dev  *get_bs_dev();
    struct spdk_blob_store *get_blobstore();
    FlameContext *get_fct();

    NvmeChunkMap *get_chunk_map();
    ChunkBlobMap *get_chunk_blob_map();

    IOChannels *get_read_channels();
    IOChannels *get_write_channels();
    struct spdk_io_channel *get_meta_channel();
    void set_meta_channel(spdk_io_channel* meta_channel);
    uint64_t get_page_size();
    uint64_t get_cluster_size();
    uint64_t get_total_chunks();
    uint32_t get_io_core(IOChannelType type, size_t index);
    struct spdk_io_channel *get_io_channel(IOChannelType type, size_t index);
    uint32_t get_meta_core();
    uint32_t get_admin_core();
    uint32_t get_core_count() const;
    uint32_t get_core_count(IOChannelType type);
    
    bool is_support_mem_persist() const;
    bool is_dirty();

    int dev_check();
    int dev_format();
    int dev_mount();
    int dev_unmount();
    bool is_mounted();
    int chunk_create(uint64_t chk_id, const chunk_create_opts_t& opts);
    int chunk_remove(uint64_t chk_id);
    bool chunk_exist(uint64_t chk_id);
    std::shared_ptr<Chunk> chunk_open(uint64_t chk_id);
    int chunk_close(Chunk *chunk);
};

class NvmeChunk : public Chunk {
private:
    //nvmestore指针，用于识别该chunk属于哪个nvmestore
    NvmeStore   *nvmestore;

    //chunk的基本信息
    uint64_t    chk_id;
    uint64_t    volume_id;
    uint32_t    volume_index;
    uint32_t    chunk_stat;
    uint32_t    chunk_spolicy;
    uint32_t    flags;
    uint64_t    chunk_size;
    uint64_t    chunk_used;
    uint64_t    ctime;
    uint64_t    dst_id;    // 只在状态为迁移时有效
    uint64_t    dst_ctime;

    //该chunk对应的blob_id，以及open之后的blob指针
    spdk_blob_id blob_id;
    struct spdk_blob *blob;

    //引用计数，以及读写计数
    uint64_t    open_ref;
    std::atomic<uint64_t>    write_counter;
    std::atomic<uint64_t>    read_counter;

    bool dirty;

    NvmeChunkState state;

    bool created;
    pthread_mutex_t create_mutex;
    pthread_cond_t  create_cond;

    bool opened;
    pthread_mutex_t open_mutex;
    pthread_cond_t  open_cond;

    bool closed;
    pthread_mutex_t close_mutex;
    pthread_cond_t  close_cond;

    bool removed;
    pthread_mutex_t remove_mutex;
    pthread_cond_t  remove_cond;

    bool persisted;
    pthread_mutex_t persist_mutex;
    pthread_cond_t  persist_cond;

    uint64_t length_to_page(uint64_t len);
    uint64_t offset_to_page(uint64_t offset);

    static void chunk_io_cb(void *cb_arg, int bserrno);
    static void chunk_io_start(void *arg1, void *arg2);

    static void delete_blob_cb(void *cb_arg, int bserrno);
    static void close_blob_cb(void *cb_arg, int bserrno);
    static void sync_md_cb(void *cb_arg, int bserrno);
    static void resize_blob_cb(void *cb_arg, int bserrno);
    static void open_blob_cb(void *cb_arg, struct spdk_blob *blb, int bserrno);
    static void chunk_create_cb(void *cb_arg, spdk_blob_id blobid, int bserrno);

    static void create_chunk_start(void *arg1, void *arg2);
    static void persist_md_start(void *arg1, void *arg2);
    static void chunk_remove_start(void *arg1, void *arg2);
    static void chunk_open_start(void *arg1, void *arg2);
    static void chunk_close_start(void *arg1, void *arg2);

public:
    NvmeChunk(FlameContext *fct, NvmeStore *store): 
                         Chunk(fct), nvmestore(store), chk_id(0), dirty(false), opened(false),
                         closed(false), removed(false), persisted(false), created(false) {
        
        volume_id = 0;
        volume_index = 0;
        chunk_stat = 0;
        chunk_spolicy = 0;
        flags = 0;
        chunk_size = 0;
        chunk_used = 0;
        ctime = 0;
        dst_id = 0;
        dst_ctime = 0;


        pthread_mutex_init(&create_mutex, NULL);
        pthread_cond_init(&create_cond, NULL);

        pthread_mutex_init(&open_mutex, NULL);
        pthread_cond_init(&open_cond, NULL);

        pthread_mutex_init(&close_mutex, NULL);
        pthread_cond_init(&close_cond, NULL);

        pthread_mutex_init(&remove_mutex, NULL);
        pthread_cond_init(&remove_cond, NULL);

        pthread_mutex_init(&persist_mutex, NULL);
        pthread_cond_init(&persist_cond, NULL);

    }

    NvmeChunk(FlameContext *fct, NvmeStore *store, uint64_t _chk_id): 
                         Chunk(fct), nvmestore(store), chk_id(_chk_id), dirty(false), opened(false),
                         closed(false), removed(false), persisted(false), created(false) {
        volume_id = 0;
        volume_index = 0;
        chunk_stat = 0;
        chunk_spolicy = 0;
        flags = 0;
        chunk_size = 0;
        chunk_used = 0;
        ctime = 0;
        dst_id = 0;
        dst_ctime = 0;
        
        pthread_mutex_init(&create_mutex, NULL);
        pthread_cond_init(&create_cond, NULL);

        pthread_mutex_init(&open_mutex, NULL);
        pthread_cond_init(&open_cond, NULL);

        pthread_mutex_init(&close_mutex, NULL);
        pthread_cond_init(&close_cond, NULL);

        pthread_mutex_init(&remove_mutex, NULL);
        pthread_cond_init(&remove_cond, NULL);

        pthread_mutex_init(&persist_mutex, NULL);
        pthread_cond_init(&persist_cond, NULL);
    }

    NvmeChunk(FlameContext *fct, NvmeStore *store, uint64_t _chk_id, 
                const chunk_create_opts_t& opts):Chunk(fct), nvmestore(store), chk_id(_chk_id),
                        volume_id(opts.vol_id), volume_index(opts.index), chunk_spolicy(opts.spolicy), 
                        flags(opts.flags), chunk_size(opts.size), dirty(false), opened(false),
                         closed(false), removed(false), persisted(false), created(false){

        pthread_mutex_init(&create_mutex, NULL);
        pthread_cond_init(&create_cond, NULL);

        pthread_mutex_init(&open_mutex, NULL);
        pthread_cond_init(&open_cond, NULL);

        pthread_mutex_init(&close_mutex, NULL);
        pthread_cond_init(&close_cond, NULL);

        pthread_mutex_init(&remove_mutex, NULL);
        pthread_cond_init(&remove_cond, NULL);

        pthread_mutex_init(&persist_mutex, NULL);
        pthread_cond_init(&persist_cond, NULL);
    }

    virtual ~NvmeChunk() {
        pthread_mutex_destroy(&create_mutex);
        pthread_cond_destroy(&create_cond);

        pthread_mutex_destroy(&open_mutex);
        pthread_cond_destroy(&open_cond);

        pthread_mutex_destroy(&close_mutex);
        pthread_cond_destroy(&close_cond);

        pthread_mutex_destroy(&remove_mutex);
        pthread_cond_destroy(&remove_cond);

        pthread_mutex_destroy(&persist_mutex);
        pthread_cond_destroy(&persist_cond);
    }

    /*
     * 同步操作，锁
    */
    void signal_create_completed() {
        pthread_mutex_lock(&create_mutex);
        created = true;
        if(created)
            pthread_cond_signal(&create_cond);
        pthread_mutex_unlock(&create_mutex);
    }

    void wait_create_completed() {
        pthread_mutex_lock(&create_mutex);
        while(!created)
            pthread_cond_wait(&create_cond, &create_mutex);
        pthread_mutex_unlock(&create_mutex);
    }

    void signal_open_completed() {
        pthread_mutex_lock(&open_mutex);
        opened = true;
        if(opened)
            pthread_cond_signal(&open_cond);
        pthread_mutex_unlock(&open_mutex);
    }

    void wait_open_completed() {
        pthread_mutex_lock(&open_mutex);
        while(!opened)
            pthread_cond_wait(&open_cond, &open_mutex);
        pthread_mutex_unlock(&open_mutex);
    }

    void signal_close_completed() {
        pthread_mutex_lock(&close_mutex);
        closed = true;
        if(closed)
            pthread_cond_signal(&close_cond);
        pthread_mutex_unlock(&close_mutex);
    }

    void wait_close_completed() {
        pthread_mutex_lock(&close_mutex);
        while(!closed)
            pthread_cond_wait(&close_cond, &close_mutex);
        pthread_mutex_unlock(&close_mutex);
    }

    void signal_remove_completed() {
        pthread_mutex_lock(&remove_mutex);
        removed = true;
        if(removed)
            pthread_cond_signal(&remove_cond);
        pthread_mutex_unlock(&remove_mutex);
    }

    void wait_remove_completed() {
        pthread_mutex_lock(&remove_mutex);
        while(!removed)
            pthread_cond_wait(&remove_cond, &remove_mutex);
        pthread_mutex_unlock(&remove_mutex);
    }

    void signal_persist_completed() {
        pthread_mutex_lock(&persist_mutex);
        persisted = true;
        if(persisted)
            pthread_cond_signal(&persist_cond);
        pthread_mutex_unlock(&persist_mutex);
    }

    void wait_persist_completed() {
        pthread_mutex_lock(&persist_mutex);
        while(!persisted)
            pthread_cond_wait(&persist_cond, &persist_mutex);
        //这里需要把persisted重新置为false，这是因为persist_md操作在一个chunk句柄上可能被执行多次
        persisted = false;
        pthread_mutex_unlock(&persist_mutex);
    }

    //基本信息获取接口
    int get_info(chunk_info_t& info) const;

    uint64_t used()     const;
    uint64_t size()     const;
    uint32_t stat()     const;
    uint64_t vol_id()   const;
    uint32_t vol_index()    const;
    uint32_t spolicy()      const;
    bool is_preallocated()  const;

    void set_dirty();
    void reset_dirty();
    bool is_dirty();

    //引用计数操作接口
    void open_ref_add(uint64_t val);
    void open_ref_sub(uint64_t val);
    uint64_t get_open_ref()         const;

    //读写计数操作接口
    void read_counter_add(uint64_t val);
    void write_counter_add(uint64_t val);
    uint64_t get_read_counter()     const;
    uint64_t get_write_counter()    const;

    //blob相关操作接口
    void set_blob(struct spdk_blob *blb);
    void set_blob_id(spdk_blob_id blobid);
    spdk_blob_id get_blob_id()      const;
    struct spdk_blob *get_blob()    const;

    uint64_t get_chunk_id() const;
    uint32_t get_target_core(IOChannelType type);
    
    //元数据操作接口
    int load();
    int store();
    int create(const chunk_create_opts_t& opts, bool sync, CHUNKOP_CALLBACK cb_fn);
    int persist_md(bool sync, CHUNKOP_CALLBACK cb_fn);
    int remove(spdk_blob_id blobid, bool sync, CHUNKOP_CALLBACK cb_fn);
    int open(spdk_blob_id, bool sync, CHUNKOP_CALLBACK cb_fn);
    int close(struct spdk_blob *blb, bool sync, CHUNKOP_CALLBACK cb_fn);

    //IO操作接口
    int read_sync(void* buff,  uint64_t off, uint64_t len);
    int write_sync(void* buff, uint64_t off, uint64_t len);

    int get_xattr(const std::string& name, std::string& value);
    int set_xattr(const std::string& name, const std::string& value);

    int read_async( void* buff,  uint64_t off, uint64_t len, chunk_opt_cb_t cb, void* cb_arg);
    int write_async(void* buff, uint64_t off, uint64_t len, chunk_opt_cb_t cb, void* cb_arg);
    
};

struct NvmeIOChannelOpArg {
    NvmeStore *nvmestore;
    struct nvmestore_io_channel *io_channel;
    int *ret;
    NvmeIOChannelOpType opcode;

public:
    NvmeIOChannelOpArg(NvmeStore *_store, 
                    struct nvmestore_io_channel *_channel, 
                    int *_ret, NvmeIOChannelOpType _type): 
                    nvmestore(_store), io_channel(_channel), 
                    ret(_ret), opcode(_type) {

    }
};

struct alloc_ch_arg : public NvmeIOChannelOpArg {
public:
    alloc_ch_arg(NvmeStore *_store, 
                    struct nvmestore_io_channel *_channel, 
                    int *_ret): 
                    NvmeIOChannelOpArg(_store, _channel, _ret, ALLOC_CHANNEL) {

    }
};

struct NvmeStoreOpArg {
    NvmeStore *nvmestore;
    int *ret;
    NvmeStoreOpType opcode;

public:
    NvmeStoreOpArg(NvmeStore *_store, 
                    int *_ret, NvmeStoreOpType _opcode):nvmestore(_store), ret(_ret), opcode(_opcode) {

    }
};

struct format_arg : public NvmeStoreOpArg {
public:
    format_arg(NvmeStore *_store, 
                int *_ret): NvmeStoreOpArg(_store, _ret, FORMAT) {

    }
};

struct mount_arg : public NvmeStoreOpArg {
public:
    mount_arg(NvmeStore *_store, 
                int *_ret): NvmeStoreOpArg(_store, _ret, MOUNT) {

    }
};

struct umount_arg : public NvmeStoreOpArg {
public:
    umount_arg(NvmeStore *_store, 
                int *_ret): NvmeStoreOpArg(_store, _ret, UNMOUNT) {

    }
};

struct create_arg : public NvmeStoreOpArg {
    NvmeChunk *chunk;
    chunk_create_opts_t *opts;
public:
    create_arg(NvmeStore *_store, 
            NvmeChunk* _chunk, int *_ret, 
            chunk_create_opts_t *_opts) : NvmeStoreOpArg(_store, _ret, CREATE_CHUNK), 
            chunk(_chunk), opts(_opts) {

    }
};

struct remove_arg : public NvmeStoreOpArg {
    spdk_blob_id blobid;
public:
    remove_arg(NvmeStore *_store, 
                spdk_blob_id _blobid, 
                int *_ret): NvmeStoreOpArg(_store, _ret, REMOVE_CHUNK), blobid(_blobid) {

    }
};

struct open_arg : public NvmeStoreOpArg {
    NvmeChunk *chunk;
public:
    open_arg(NvmeStore *_store, 
            NvmeChunk *_chunk, int *_ret): NvmeStoreOpArg(_store, _ret, OPEN_CHUNK), 
            chunk(_chunk) {

    }
};

struct close_arg : public NvmeStoreOpArg {
    NvmeChunk *chunk;
public:
    close_arg(NvmeStore *_store, 
            NvmeChunk *_chunk, int *_ret): NvmeStoreOpArg(_store, _ret, CLOSE_CHUNK), 
            chunk(_chunk) {

    }
};

struct NvmeChunkOpArg {    
    NvmeChunkOpType opcode;
    NvmeChunk *chunk;
    int *ret;
    bool sync;

    //void (*cb_fn)(NvmeChunkOpArg *, int);
    CHUNKOP_CALLBACK cb_fn;

public:
    NvmeChunkOpArg(NvmeChunk *_chunk, int *_ret, 
                NvmeChunkOpType _op, bool _sync, CHUNKOP_CALLBACK _cb_fn): chunk(_chunk), 
                                    ret(_ret), opcode(_op), sync(_sync), cb_fn(_cb_fn) {

    }

    virtual ~NvmeChunkOpArg() {

    }
};

struct chunk_create_arg : public NvmeChunkOpArg {
    const chunk_create_opts_t *opts;
public:
    chunk_create_arg(NvmeChunk *_chunk, int *_ret, 
                const chunk_create_opts_t *_opts, bool _sync, CHUNKOP_CALLBACK _cb_fn): 
                    NvmeChunkOpArg(_chunk, _ret, CHUNK_CREATE, _sync, _cb_fn), 
                    opts(_opts) {

    }

    ~chunk_create_arg() {

    }
};

struct persist_arg : public NvmeChunkOpArg {
public:
    persist_arg(NvmeChunk *_chunk, int *_ret, bool _sync, CHUNKOP_CALLBACK _cb_fn): 
                NvmeChunkOpArg(_chunk, _ret, CHUNK_PERSIST_MD, _sync, _cb_fn) {

    }

    ~persist_arg() {

    }
};

struct chunk_open_arg : public NvmeChunkOpArg {
    spdk_blob_id blobid;
public:
    chunk_open_arg(NvmeChunk *_chunk, int *_ret, spdk_blob_id _blobid, 
                bool _sync, CHUNKOP_CALLBACK _cb_fn):
                    NvmeChunkOpArg(_chunk, _ret, CHUNK_OPEN, _sync, _cb_fn), 
                    blobid(_blobid) {

    }

    ~chunk_open_arg() {

    }
};

struct chunk_remove_arg : public NvmeChunkOpArg {
    spdk_blob_id blobid;
public:
    chunk_remove_arg(NvmeChunk *_chunk, int *_ret, spdk_blob_id _blobid, 
                bool _sync, CHUNKOP_CALLBACK _cb_fn): 
                NvmeChunkOpArg(_chunk, _ret, CHUNK_REMOVE, _sync, _cb_fn), 
                blobid(_blobid) {

    }

    ~chunk_remove_arg() {

    }
};

struct chunk_close_arg : public NvmeChunkOpArg {
    struct spdk_blob *blb;
public:
    chunk_close_arg(NvmeChunk *_chunk, int *_ret, struct spdk_blob *_blb, bool _sync, CHUNKOP_CALLBACK _cb_fn): 
                NvmeChunkOpArg(_chunk, _ret, CHUNK_CLOSE, _sync, _cb_fn), blb(_blb) {

    }

    ~chunk_close_arg() {

    }
};

struct chunk_write_arg : public NvmeChunkOpArg {
    uint64_t offset;
    uint64_t length;
    void *buff;
    void *cb_arg;
    chunk_opt_cb_t wr_cb;
public:
    chunk_write_arg(NvmeChunk *_chunk, int *_ret, bool _sync, CHUNKOP_CALLBACK _cb_fn,
                        uint64_t _offset, uint64_t _length, void *_buffer,
                        chunk_opt_cb_t _wr_cb, void *_cb_arg): 
                            NvmeChunkOpArg(_chunk, _ret, CHUNK_WRITE, _sync, _cb_fn), 
                            offset(_offset), length(_length), buff(_buffer), 
                            wr_cb(_wr_cb), cb_arg(_cb_arg) {

    }

    ~chunk_write_arg() {

    }
};

struct chunk_read_arg : public NvmeChunkOpArg {
    uint64_t offset;
    uint64_t length;
    void *buff;
    void *cb_arg;
    chunk_opt_cb_t rd_cb;
public:
    chunk_read_arg(NvmeChunk *_chunk, int *_ret, bool _sync, CHUNKOP_CALLBACK _cb_fn, 
                    uint64_t _offset, uint64_t _length, void *_buffer, 
                    chunk_opt_cb_t _rd_cb, void *_cb_arg): 
                        NvmeChunkOpArg(_chunk, _ret, CHUNK_READ, _sync, _cb_fn),
                        offset(_offset), length(_length), buff(_buffer), 
                        rd_cb(_rd_cb), cb_arg(_cb_arg) {

    }
    
    ~chunk_read_arg() {

    }
};

}

#endif