#include "mgr/volm/vol_mgmt.h"
#include "util/utime.h"

#include <climits>

#include "mgr/volm/log_volm.h"

using namespace std;

namespace flame {

int VolumeManager::init() {
    int r;
    r = init_vg__();
    if (r != RC_SUCCESS) 
        return r;
    return RC_SUCCESS;
}

/**
 * Volume Group
*/
int VolumeManager::vg_list(list<volume_group_meta_t>& res_list, uint32_t off, uint32_t limit) {
    bct_->log()->ltrace("off=%u, limit=%u", off, limit);

    ReadLocker locker(vg_lock_);
    auto it = vg_idx_.begin();
    while (off > 0 && it != vg_idx_.end())
        off--, it++;

    if (limit == 0) 
        limit = UINT32_MAX;
    while (limit > 0 && it != vg_idx_.end()) {
        res_list.push_back(*it->second);
        limit--;
        it++;
    }

    return RC_SUCCESS;
}

int VolumeManager::vg_create(const string& name) {
    bct_->log()->ltrace("name=%s", name.c_str());

    WriteLocker locker(vg_lock_);
    volume_group_meta_t vg;
    vg.vg_id = max_vg_id_++;
    vg.name = name;
    vg.ctime = utime_t::now().to_usec();
    int r = ms_->get_vg_ms()->create(vg);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("persist new vg faild: %d", r);
        return r;
    }

    volume_group_meta_t& meta = vg_map_[vg.vg_id];
    meta = vg;
    vg_idx_[meta.name] = &meta;

    return RC_SUCCESS;
}

int VolumeManager::vg_remove(const string& name) {
    bct_->log()->ltrace("name=%s", name.c_str());

    WriteLocker locker(vg_lock_);

    auto it = vg_idx_.find(name);
    if (it == vg_idx_.end()) {
        bct_->log()->lerror("vg object can't be found in vg_idx_");
        return RC_OBJ_NOT_FOUND;
    }

    if (it->second->volumes > 0) {
        bct_->log()->lerror("vg is not emptry");
        return RC_REFUSED;
    }

    int r = ms_->get_vg_ms()->remove(name);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("remove vg faild: %d", r);
        return r;
    }

    uint64_t vg_id = it->second->vg_id;
    vg_idx_.erase(it);
    vg_map_.erase(vg_id);

    return RC_SUCCESS;
}

int VolumeManager::vg_rename(const string& old_name, const string& new_name) {
    bct_->log()->ltrace("name=%s, new=%s", old_name.c_str(), new_name.c_str());

    WriteLocker locker(vg_lock_);
    auto it = vg_idx_.find(old_name);
    if (it == vg_idx_.end()) {
        bct_->log()->lerror("vg object not found");
        return RC_OBJ_NOT_FOUND;
    }

    auto nit = vg_idx_.find(new_name);
    if (nit != vg_idx_.end()) {
        bct_->log()->lerror("new vg name have been existed");
        return RC_OBJ_EXISTED;
    }

    auto vg = it->second;
    vg_idx_.erase(it);
    vg_idx_[new_name] = vg;
    vg->name = new_name;

    return RC_SUCCESS;
}

/**
 * Volume
 */
int VolumeManager::vol_list(list<volume_meta_t>& res_list, const string& vg_name, uint32_t offset, uint32_t limit) {
    bct_->log()->ltrace("vg=%s, off=%u, limit=%u", vg_name.c_str(), offset, limit);

    uint64_t vg_id = get_vg_id__(vg_name);
    if (vg_id == 0) {
        bct_->log()->lerror("vg not found");
        return RC_OBJ_NOT_FOUND;
    }

    return ms_->get_volume_ms()->list(res_list, vg_id, offset, limit);
}

int VolumeManager::vol_create(const string& vg_name, const string& vol_name, const vol_attr_t& attr) {
    bct_->log()->ltrace("vg=%s, vol=%s, sz=%lluG, flg=%u, sp=%u", vg_name.c_str(), vol_name.c_str(), attr.size >> 30, attr.flags, attr.spolicy);

    int r;
    // get_vg_id_add_vol__() 原子获取vg_id并增加vol计数，防止创建Volume过程中，VG被误删除
    uint64_t vg_id = get_vg_id_add_vol__(vg_name);
    if (vg_id == 0) {
        bct_->log()->lerror("vg not found");
        return RC_OBJ_NOT_FOUND;
    }

    spolicy::StorePolicy* sp = bct_->get_spolicy(attr.spolicy);
    if (sp == nullptr) {
        bct_->log()->lerror("wrong store policy type: %d", attr.spolicy);
        vg_sub_vol_cnt__(vg_id);
        return RC_WRONG_PARAMETER;
    }

    // 在数据库中创建volume，并get到vol_id
    volume_meta_t vol;
    vol.vg_id = vg_id;
    vol.name = vol_name;
    vol.stat = VOL_STAT_CREATING; // volume的状态为创建中！
    vol.chk_sz = attr.chk_sz;
    vol.size = attr.size;
    vol.flags = attr.flags;
    vol.spolicy = attr.spolicy;
    vol.used = 0;
    vol.chunks = attr.size / attr.chk_sz;
    r = ms_->get_volume_ms()->create_and_get(vol);//先写库
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("insert volume into database faild vg(%s) volume(%s) ", vg_name.c_str(), vol.name.c_str());
        vg_sub_vol_cnt__(vg_id);
        return r;
    }

    bct_->log()->ltrace("vol_id=%llu", vol.vol_id);

    chk_attr_t chka;
    chka.spolicy = attr.spolicy;
    chka.flags = attr.flags;
    chka.size = attr.chk_sz;

    chunk_id_t chk_id;
    chk_id.set_vol_id(vol.vol_id);
    chk_id.set_index(0);

    uint64_t cg_size = sp->cg_size(); // 每个CG的有效大小（不一定是实际占用空间）
    uint8_t cgn = sp->cgn();
    uint32_t grp = vol.size / cg_size;
    grp += vol.size % cg_size ? 1 : 0;

    r = chkm_->create_vol(chk_id, grp, cgn, chka);
    if (r != RC_SUCCESS) {
        // 暂时还没有处理Chunk创建失败的情况，直接返回Volume创建失败
        bct_->log()->lerror("create chunk faild");
        return RC_INTERNAL_ERROR;
    }

    // 更新Volume信息与状态
    vol.size = grp * cg_size; // 更新Volume实际大小
    vol.stat = VOL_STAT_CREATED;
    r = ms_->get_volume_ms()->update(vol);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("update volume stat to creating success error vg(%s) volume(%s)", vg_name.c_str(), vol.name.c_str());
        return r;
    }
    
    vg_add_vol_sz__(vol.vg_id, vol.size);

    return RC_SUCCESS;
}

int VolumeManager::vol_remove(const std::string& vg_name, const std::string& vol_name) {
    bct_->log()->ltrace("vg=%s, vol=%s", vg_name.c_str(), vol_name.c_str());

    uint64_t vg_id = get_vg_id__(vg_name);
    if (vg_id == 0) {
        bct_->log()->lerror("vg not found");
        return RC_OBJ_NOT_FOUND;
    }

    int r;
    volume_meta_t vol;
    r = ms_->get_volume_ms()->get(vol, vg_id, vol_name);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("get volume faild");
        return r;
    }

    if (vol.stat == VOL_STAT_CREATING) {
        bct_->log()->lerror("try to delete a creating volume");
        return RC_OBJ_NOT_FOUND;
    }
    
    // 当前的方案存在瑕疵，后期应该为Volume添加一个Handle防止重复删除的情况
    // 目前，重复删除也不会到来严重的问题，但是会增加带宽占用
    if (vol.stat == VOL_STAT_DELETING) {
        bct_->log()->lerror("multiple delete same volume");
        return RC_MULTIPLE_OPERATE;
    }

    // 将volume的状态改为deleting
    vol.stat = VOL_STAT_DELETING;
    r = ms_->get_volume_ms()->update(vol);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("change volume's status faild");
        return r;
    }

    r = chkm_->remove_vol(vol.vol_id);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("remove volume's chunk faild");
        return r;
    }

    r = ms_->get_volume_ms()->remove(vol.vol_id);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("remove volume form metastore faild");
        return r;
    }

    return RC_SUCCESS;
}


int VolumeManager::vol_rename(const std::string& vg_name, const std::string& vol_old_name, const std::string& vol_new_name) {
    bct_->log()->ltrace("vg=%s, vol=%s, new=%s", vg_name.c_str(), vol_old_name.c_str(), vol_new_name.c_str());
    // @@@ don't support now
    return RC_FAILD;
}

int VolumeManager::vol_info(volume_meta_t& vol_res, const std::string& vg_name, const std::string& vol_name) {
    bct_->log()->ltrace("vg=%s, vol=%s, new=%s", vg_name.c_str(), vol_name.c_str());
    uint64_t vg_id = get_vg_id__(vg_name);
    if (vg_id == 0) {
        bct_->log()->lerror("vg not found");
        return RC_OBJ_NOT_FOUND;
    }

    return ms_->get_volume_ms()->get(vol_res, vg_id, vol_name);
}

int VolumeManager::vol_resize(const std::string& vg_name, const std::string& vol_name, uint64_t new_size) {
    // @@@ don't support now
    return RC_FAILD;
}

int VolumeManager::vol_open(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name, VolumeMeta &volume_meta) {
    bct_->log()->ltrace("vg=%s, volume=%s", vg_name.c_str(), vol_name.c_str());

    uint64_t vg_id = get_vg_id__(vg_name);
    if (vg_id == 0) {
        bct_->log()->lerror("vg not found");
        return RC_OBJ_NOT_FOUND;
    }
    volume_meta_t vol_meta;
    int rc = ms_->get_volume_ms()->get(vol_meta, vg_id, vol_name);
    volume_meta.id = vol_meta.vol_id;
    volume_meta.name = vol_name;
    volume_meta.vg_name = vg_name;
    volume_meta.size = vol_meta.size;
    volume_meta.ctime = vol_meta.ctime;
    // volume_meta.prealloc = vol_meta.;
    volume_meta.chk_sz = vol_meta.chk_sz;
    volume_meta.spolicy = vol_meta.spolicy;
    return rc;
}

int VolumeManager::vol_close(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name) {
    // @@@ don't support now
    return RC_FAILD;
}

int VolumeManager::vol_lock(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name) {
    // @@@ don't support now
    return RC_FAILD;
}

int VolumeManager::vol_unlock(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name) {
    // @@@ don't support now
    return RC_FAILD;
}

volume_group_meta_t* VolumeManager::get_vg__(uint64_t vg_id) {
    return &vg_map_[vg_id];
}

uint64_t VolumeManager::get_vg_id__(const std::string& vg_name) {
    ReadLocker locker(vg_lock_);
    auto it = vg_idx_.find(vg_name);
    if (it == vg_idx_.end())
        return 0;
    return it->second->vg_id;
}

uint64_t VolumeManager::get_vg_id_add_vol__(const std::string& vg_name) {
    WriteLocker locker(vg_lock_);
    auto it = vg_idx_.find(vg_name);
    if (it == vg_idx_.end())
        return 0;
    it->second->volumes += 1;
    return it->second->vg_id;
}

int VolumeManager::vg_add_vol_cnt__(uint64_t vg_id) {
    WriteLocker locker(vg_lock_);
    auto it = vg_map_.find(vg_id);
    if (it == vg_map_.end())
        return RC_OBJ_NOT_FOUND;
    it->second.volumes += 1;
    return RC_SUCCESS;
}

int VolumeManager::vg_add_vol_sz__(uint64_t vg_id, uint64_t sz) {
    WriteLocker locker(vg_lock_);
    auto it = vg_map_.find(vg_id);
    if (it == vg_map_.end())
        return RC_OBJ_NOT_FOUND;
    it->second.size += sz;
    
    return ms_->get_vg_ms()->update(it->second);
}

int  VolumeManager::vg_sub_vol_cnt__(uint64_t vg_id) {
    WriteLocker locker(vg_lock_);
    auto it = vg_map_.find(vg_id);
    if (it == vg_map_.end())
        return RC_OBJ_NOT_FOUND;
    it->second.volumes -= 1;
    return RC_SUCCESS;
}

int VolumeManager::vg_sub_vol__(uint64_t vg_id, uint64_t sz) {
    WriteLocker locker(vg_lock_);
    auto it = vg_map_.find(vg_id);
    if (it == vg_map_.end())
        return RC_OBJ_NOT_FOUND;
    it->second.volumes -= 1;
    it->second.size -= sz;
    
    return ms_->get_vg_ms()->update(it->second);
}

int VolumeManager::init_vg__() {
    list<volume_group_meta_t> vgs;
    int r = ms_->get_vg_ms()->list(vgs, 0, 0);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("init vg faild");
        return r;
    }

    for (auto it = vgs.begin(); it != vgs.end(); ++it) {
        vg_map_[it->vg_id] = *it;
        vg_idx_[it->name] = &(*it);
        if (max_vg_id_ < it->vg_id)
            max_vg_id_ = it->vg_id;
    }
    max_vg_id_ += 1;

    return RC_SUCCESS;
}

} // namespace flame