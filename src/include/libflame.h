/**
 * @file libflame.h
 * @author zhzane (zhzane@outlook.com)
 * @brief 
 * @version 0.1
 * @date 2019-05-09
 * 
 * @copyright Copyright (c) 2019
 * 
 */
#ifndef LIBFLAME_H
#define LIBFLAME_H

#include "include/retcode.h"
#include "include/buffer.h"

#include <cstdint>
#include <string>
#include <vector>

#if __GNUC__ >= 4
    #define FLAME_API __attribute__((visibility ("default")))
#else
    #define FLAME_API
#endif

namespace libflame {

typedef void (*callback_fn_t)(int rc, void* arg1, void* arg2);

struct FLAME_API Config {
    std::string mgr_addr;

}; // class Config

struct FLAME_API AsyncCallback {
    callback_fn_t fn { nullptr };
    void* arg1 { nullptr };
    void* arg2 { nullptr };

    inline void call(int rc) {
        if (fn != nullptr) fn(rc, arg1, arg2);
    }
};

class FLAME_API Volume;
class FLAME_API VolumeAttr;
class FLAME_API VolumeMeta;

class FLAME_API FlameStub {
public:
    static FlameStub* connect(const Config& cfg);
    static FlameStub* connect(std::string& path);
    int shutdown();

    // Cluster API
    // return info of cluster by a argurment
    int cluster_info(const std::string& arg, std::string& rst);

    // Group API
    // create an group.
    int vg_create(const std::string& name);
    // list group. return an list of group name.
    int vg_list(std::vector<std::string>>& rst);
    // remove an group.
    int vg_remove(const std::string& name);
    // rename an group. not support now
    // int vg_rename(const std::string& src, const std::string& dst);

    // Volume API
    // create an volume.
    int vol_create(const std::string& group, const std::string& name, const VolumeAttr& attr);
    // list volumes. return an list of volume name.
    int vol_list(const std::string& group, std::vector<std::string>>& rst);
    // remove an volume.
    int vol_remove(const std::string& group, const std::string& name);
    // rename an volume. not support now
    // int vol_rename(const std::string& group, const std::string& src, const std::string& dst);
    // move an volume to another group.
    // int vol_move(const std::string& group, const std::string& name, const std::string& target);
    // clone an volume
    // int vol_clone(const std::string& src_group, const std::string& src_name, const std::string& dst_group, const std::string& dst_name);
    // read info of volume.
    int vol_meta(const std::string& group, const std::string& name, VolumeMeta& info);
    // open an volume, and return the io context of volume.
    int vol_open(const std::string& group, const std::string& name, Volume** rst);
    // lock an volume. not support now
    // int vol_open_locked(const std::string& group, const std::string& name, Volume** rst);
    // unlock an volume. not support now
    // int vol_unlock(const std::string& group, const std::string& name);

private:
    FlameStub();
    ~FlameStub();

    FlameStub(const Cluster& rhs) = delete;
    FlameStub& operator=(const Cluster& rhs) = delete;

}; // class Cluster

class FLAME_API Volume {
public:
    uint64_t get_id();
    std::string get_name();
    std::string get_group();
    uint64_t size();
    VolumeMeta get_meta();

    // manage: not support, now
    // int resize(uint64_t size);
    // int lock();
    // int unlock();

    // async io call
    int read(const BufferList& buffs, uint64_t offset, uint64_t len, const AsyncCallback& cb);
    int write(const BufferList& buffs, uint64_t offset, uint64_t len, const AsyncCallback& cb);
    int reset(uint64_t offset, uint64_t len, const AsyncCallback& cb);
    int flush(const AsyncCallback& cb);

private:
    Volume();
    ~Volume;

    friend class Cluster;
}; // class Volume

class FLAME_API VolumeAttr {
public:
    VolumeAttr(uint64_t size) : size_(size), prealloc_(false) {}
    ~VolumeAttr() {}

    uint64_t get_size() const { return size_; }
    void set_size(uint64_t s) { size_ = s; }

    bool is_prealloc() const { return prealloc_; }
    void set_prealloc(bool prealloc) { prealloc_ = prealloc; }

private:
    uint64_t size_;
    bool prealloc_;
}; // struct VolumeAttr

class FLAME_API VolumeMeta {
public:
    VolumeMeta() {}
    ~VolumeMeta() {}
    
    uint64_t get_id() const { return id_; }
    std::string& get_name() const { return name_; }
    std::string& get_group() const { return group_; }
    uint64_t get_size() const { return size_; }
    uint64_t get_ctime() const { return ctime_; }
    bool is_prealloc() const { return prealloc_; }

private:
    friend class Volume;

    void set_id(uint64_t id) { id_ = id; }
    void set_name(const std::string& name) { name_ = name; }
    void set_group(const std::string& group) { group_ = group; }
    void set_size(uint64_t size) { size_ = size; }
    void set_ctime(uint64_t ctime) { ctime_ = ctime; }
    void set_prealloc(bool v) { prealloc_ = v; } 

    uint64_t id_;
    std::string name_;
    std::string group_;
    uint64_t size_;
    uint64_t ctime_; // create time
    bool prealloc_;
}; // class VolumeMeta

} // namespace libflame

#endif // LIBFLAME_H