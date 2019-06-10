#ifndef CHUNKSTORE_CHUNKBLOBMAP_H
#define CHUNKSTORE_CHUNKBLOBMAP_H

#include <iostream>
#include <map>
#include <vector>

#include "spdk/stdinc.h"
#include "spdk/bdev.h"
#include "spdk/event.h"
#include "spdk/env.h"
#include "spdk/blob_bdev.h"
#include "spdk/blob.h"
#include "spdk/log.h"
#include "spdk/version.h"
#include "spdk/string.h"

#include "chunkstore/nvmestore/nvmestore.h"

namespace flame {
class NvmeStore;

enum ChunkBlobMapError {
    CREATE_BLOB_ERR = 1,
    OPEN_BLOB_ERR,
    READ_BLOB_ERR,
    WRITE_BLOB_ERR,
    CLOSE_BLOB_ERR,
    LOAD_BLOB_ERR,
    STORE_BLOB_ERR,
    BLOB_CLOSED
};

class ChunkBlobMap {
private:
    NvmeStore *nvmestore;
    uint64_t loaded_entries;
    uint64_t entry_nums;

    std::map<uint64_t, spdk_blob_id> chunk_blob_map;
    spdk_blob_id map_blob_id;
    struct spdk_blob *map_blob;

    static void load_cb(void *cb_arg, int bserrno);
    static void load_next_cb(void *cb_arg, int bserrno);

    bool dirty;

    bool created;
    pthread_mutex_t create_mutex;
    pthread_cond_t create_cond;

    bool loaded;
    pthread_mutex_t load_mutex;
    pthread_cond_t load_cond;

    bool stored;
    pthread_mutex_t store_mutex;
    pthread_cond_t store_cond;

    bool closed;
    pthread_mutex_t close_mutex;
    pthread_cond_t close_cond;

public:
    ChunkBlobMap(NvmeStore *_store): nvmestore(_store), 
                        loaded_entries(0), entry_nums(0), 
                        map_blob_id(0), map_blob(nullptr), 
                        dirty(false), created(false), loaded(false), 
                        stored(false), closed(true) {
        pthread_mutex_init(&create_mutex, NULL);
        pthread_cond_init(&create_cond, NULL);

        pthread_mutex_init(&load_mutex, NULL);
        pthread_cond_init(&load_cond, NULL);

        pthread_mutex_init(&store_mutex, NULL);
        pthread_cond_init(&store_cond, NULL); 

        pthread_mutex_init(&close_mutex, NULL);
        pthread_cond_init(&close_cond, NULL);

    }

    ChunkBlobMap(NvmeStore *_store, spdk_blob_id _id): nvmestore(_store), 
                        map_blob_id(_id), loaded_entries(0), 
                        entry_nums(0), map_blob(nullptr), dirty(false),
                        created(true), loaded(false), 
                        stored(false), closed(true) {
        pthread_mutex_init(&create_mutex, NULL);
        pthread_cond_init(&create_cond, NULL);

        pthread_mutex_init(&load_mutex, NULL);
        pthread_cond_init(&load_cond, NULL);

        pthread_mutex_init(&store_mutex, NULL);
        pthread_cond_init(&store_cond, NULL);

        pthread_mutex_init(&close_mutex, NULL);
        pthread_cond_init(&close_cond, NULL);      
    }

    ~ChunkBlobMap() {
        pthread_mutex_destroy(&create_mutex);
        pthread_cond_destroy(&create_cond);

        pthread_mutex_destroy(&load_mutex);
        pthread_cond_destroy(&load_cond);

        pthread_mutex_destroy(&store_mutex);
        pthread_cond_destroy(&store_cond);

        pthread_mutex_destroy(&close_mutex);
        pthread_cond_destroy(&close_cond);
    }

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

    void signal_load_completed() {
        pthread_mutex_lock(&load_mutex);
        loaded = true;
        if(loaded)
            pthread_cond_signal(&load_cond);
        pthread_mutex_unlock(&load_mutex);
    }

    void wait_load_completed() {
        pthread_mutex_lock(&load_mutex);
        while(!loaded)
            pthread_cond_wait(&load_cond, &load_mutex);
        pthread_mutex_unlock(&load_mutex);

    }

    void signal_store_completed() {
        pthread_mutex_lock(&store_mutex);
        stored = true;
        if(stored)
            pthread_cond_signal(&store_cond);
        pthread_mutex_unlock(&store_mutex);
    }

    void wait_store_completed() {
        pthread_mutex_lock(&store_mutex);
        while(!stored)
            pthread_cond_wait(&store_cond, &store_mutex);
        stored = false;
        pthread_mutex_unlock(&store_mutex);
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

    //load 操作辅助函数
    static void read_next_page_cb(void *cb_arg, int bserrno);
    static void read_map_blob_cb(void *cb_arg, int bserrno);
    static void read_map_blob(void *arg1, void *arg2);
    static void delete_blob_cb(void *cb_arg, int bserrno);
    static void open_map_blob_cb(void *cb_arg, struct spdk_blob *blb, int bserrno);
    static void load_map_start(void *arg1, void *arg2);
    
    //load接口函数
    int load();

    //store 操作辅助函数
    static void store_cb(void *cb_arg, int bserrno);
    static void store_map_start(void *arg1, void *arg2);

    //store接口函数
    int store();

    //create 操作辅助函数
    static void write_init_info_cb(void *cb_arg, int bserrno);
    static void write_init_info(void *arg1, void *arg2);
    static void create_map_blob_cb(void *cb_arg, spdk_blob_id blobid, int bserrno);
    static void create_map_blob_start(void *arg1, void *arg2);

    //create接口函数
    int create_blob(struct spdk_blob_opts &opts);

    //close 操作辅助函数
    static void close_blob_cb(void *cb_arg, int bserrno);
    static void close_blob_start(void *arg1, void *arg2);

    //close接口函数
    int close_blob();

    //元数据接口函数
    void set_blob_id(spdk_blob_id blobid);
    void set_blob(struct spdk_blob *blb);
    bool chunk_exist(uint64_t chk_id);
    void insert_entry(uint64_t chk_id, spdk_blob_id blobid);
    void remove_entry(uint64_t blobid);
    spdk_blob_id get_blob_id(uint64_t chk_id);
    spdk_blob_id get_chunk_map_blob_id() const;
    uint64_t get_entry_nums();
    bool is_dirty();
    void cleanup();
};

enum ChunkBlobMapOpType {
    CREATE = 0,
    CLOSE,
    LOAD,
    STORE
};

struct ChunkBlobMapOpArg {
    ChunkBlobMapOpType opcode;
    ChunkBlobMap *chunk_blob_map;
    NvmeStore *nvmestore;
    int *ret;

public:
    ChunkBlobMapOpArg(ChunkBlobMap *_map, 
                    NvmeStore *_store, int *_ret, ChunkBlobMapOpType _op): chunk_blob_map(_map), 
                                    nvmestore(_store), ret(_ret), opcode(_op) {

    }

    virtual ~ChunkBlobMapOpArg() { }
};

struct create_map_arg : public ChunkBlobMapOpArg {
    struct spdk_blob_opts *opts;
    void *buffer;
public:
    create_map_arg(ChunkBlobMap *_map,
                NvmeStore *_store, int *_ret, 
                struct spdk_blob_opts *_opts, void *_buff): 
                ChunkBlobMapOpArg(_map, _store, _ret, CREATE), 
                opts(_opts), buffer(_buff){

    }
};

struct close_map_arg : public ChunkBlobMapOpArg {
public:
    close_map_arg(ChunkBlobMap *_map, NvmeStore *_store, 
                    int *_ret): ChunkBlobMapOpArg(_map, _store, _ret, CLOSE) {

    }
};

struct load_map_arg : public ChunkBlobMapOpArg {
    void *buffer;
public:
    load_map_arg(ChunkBlobMap *_map, 
                NvmeStore *_store, int *_ret, 
                void *_buff): ChunkBlobMapOpArg(_map, _store, _ret, LOAD), 
                            buffer(_buff) {

    }
};

struct store_map_arg : public ChunkBlobMapOpArg {
    void *buff;
    uint64_t length;
public:
    store_map_arg(ChunkBlobMap *_map, NvmeStore *_store, 
                        int *_ret, void *_buff, uint64_t _length): 
                            ChunkBlobMapOpArg(_map, _store, _ret, STORE), 
                            buff(_buff), length(_length) {

    }
};

struct chunk_blob_entry_descriptor {
    uint64_t entry_nums_total;
    uint64_t rest;
public:
    chunk_blob_entry_descriptor(): entry_nums_total(0), rest(0) {
        
    }
};

}

#endif