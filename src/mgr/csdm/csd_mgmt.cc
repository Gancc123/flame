#include "mgr/csdm/csd_mgmt.h"
#include "include/retcode.h"
#include "log_csdm.h"
#include <cassert>

namespace flame {

/**
 * CsdManager
 */

int CsdManager::init() {
    // pass: 从MetaStore加载所有CSD信息
    // 初始化next_csd_id
    std::list<csd_meta_t> res_list;
    int r = ms_->get_csd_ms()->list_all(res_list);
    if (r != 0) return r;
    
    uint64_t max_id = 0;

    for (auto it = res_list.begin(); it != res_list.end(); ++it) {
        CsdHandle* hdl = create_csd_handle__(it->csd_id);
        hdl->obj_as_load__();
        hdl->get()->meta() = *it;
        hdl->set_latime__(utime_t::get_by_usec(it->latime));
        if (!insert_csd_handle__(it->csd_id, hdl)) 
            delete hdl;
        if (max_id < it->csd_id) 
            max_id = it->csd_id;
    }

    next_csd_id_ = max_id + 1;

    std::list<csd_health_meta_t> hlt_list;
    r = ms_->get_csd_health_ms()->list_all(hlt_list);
    if (r != 0) return r;

    for (auto it = hlt_list.begin(); it != hlt_list.end(); ++it) {
        auto mit = csd_map_.find(it->csd_id);
        if (mit != csd_map_.end()) {
            CsdHandle* hdl = mit->second;
            hdl->get()->health() = *it;
            hdl->obj_as_save__();
        }
    }

    return RC_SUCCESS;
}

int CsdManager::csd_register(const csd_reg_attr_t& attr, CsdHandle** hp) {
    assert(hp != nullptr);

    uint64_t new_id = next_csd_id_.fetch_add(1);

    CsdHandle* hdl = create_csd_handle__(new_id);
    hdl->obj_as_new__();
    
    CsdObject* obj = hdl->get();
    csd_meta_t& meta = obj->meta();

    meta.csd_id = new_id;
    meta.name = attr.csd_name;
    meta.size = attr.size;
    meta.io_addr = attr.io_addr;
    meta.admin_addr = attr.admin_addr;
    meta.stat = attr.stat;
    meta.ctime = utime_t::now().to_usec();
    meta.latime = utime_t::now().to_usec();

    WriteLocker map_locker(csd_map_lock_);
    if (!insert_csd_handle__(new_id, hdl)) {
        delete hdl;
        fct_->log()->lerror("register csd ($llu, %s) faild: duplicated map key", new_id, attr.csd_name.c_str());
        return RC_OBJ_EXISTED;
    }

    ReadLocker hdl_locker(hdl->get_lock());
    int r = hdl->save();
    if (r != RC_SUCCESS) {
        csd_map_.erase(new_id);
        fct_->log()->lerror("register csd ($llu, %s) faild: save error", new_id, attr.csd_name.c_str());
        return r;
    }
    *hp = hdl;
    return RC_SUCCESS;
}

int CsdManager::csd_unregister(uint64_t csd_id) {
    WriteLocker map_locker(csd_map_lock_);
    
    auto it = csd_map_.find(csd_id);
    if (it == csd_map_.end()) 
        return RC_OBJ_NOT_FOUND;
    
    CsdHandle* hdl = it->second;
    hdl->obj_as_trim__();
    int r = ms_->get_csd_ms()->remove(csd_id);

    if (r != RC_SUCCESS) {
        fct_->log()->lerror("unregister csd faild: %llu", csd_id);
        return r;
    }

    csd_map_.erase(it);
    return RC_SUCCESS;
}

int CsdManager::csd_sign_up(const csd_sgup_attr_t& attr) {
    ReadLocker map_locker(csd_map_lock_);

    auto it = csd_map_.find(attr.csd_id);
    if (it == csd_map_.end()) 
        return RC_OBJ_NOT_FOUND;

    CsdHandle* hdl = it->second;
    WriteLocker hdl_locker(hdl->get_lock());

    if (!hdl->obj_is_new__()) {
        hdl->obj_as_dirty__();
    }

    CsdObject* obj = hdl->get();
    obj->set_stat(attr.stat);
    obj->set_io_addr(attr.io_addr);
    obj->set_admin_addr(attr.admin_addr);

    if (hdl->save() == RC_SUCCESS) {
        hdl->obj_as_save__();
    }

    hdl->update_stat(CSD_STAT_PAUSE);
    
    return RC_SUCCESS;
}

int CsdManager::csd_sign_out(uint64_t csd_id) {
    ReadLocker map_locker(csd_map_lock_);

    auto it = csd_map_.find(csd_id);
    if (it == csd_map_.end()) 
        return RC_OBJ_NOT_FOUND;

    CsdHandle* hdl = it->second;
    WriteLocker hdl_locker(hdl->get_lock());

    if (!hdl->obj_is_new__()) {
        hdl->obj_as_dirty__();
    }

    CsdObject* obj = hdl->get();
    obj->set_stat(CSD_STAT_DOWN);

    if (hdl->save() == RC_SUCCESS) {
        hdl->obj_as_save__();
    }

    hdl->update_stat(CSD_STAT_DOWN);

    return RC_SUCCESS;
}

int CsdManager::csd_stat_update(uint64_t csd_id, uint32_t stat) {
    ReadLocker map_locker(csd_map_lock_);

    auto it = csd_map_.find(csd_id);
    if (it == csd_map_.end()) {
        return RC_OBJ_NOT_FOUND;
    }

    it->second->update_stat(stat);

    return RC_SUCCESS;
}

int CsdManager::csd_health_update(uint64_t csd_id, const csd_hlt_sub_t& hlt) {
    ReadLocker map_locker(csd_map_lock_);

    auto it = csd_map_.find(csd_id);
    if (it == csd_map_.end()) {
        return RC_OBJ_NOT_FOUND;
    }
    
    it->second->update_health(hlt);
   
    return RC_SUCCESS;
}

CsdHandle* CsdManager::find(uint64_t csd_id) {
    ReadLocker map_locker(csd_map_lock_);
    auto it = csd_map_.find(csd_id);
    return it == csd_map_.end() ? nullptr : it->second;
}

CsdHandle* CsdManager::create_csd_handle__(uint64_t csd_id) {
    return new CsdHandle(this, ms_, csd_id);
}

bool CsdManager::insert_csd_handle__(uint64_t csd_id, CsdHandle* hdl) {
    auto it = csd_map_.find(csd_id);
    if (it == csd_map_.end()) {
        csd_map_[csd_id] = hdl;
        return true;
    }
    return false;
}

void CsdManager::destroy__() {
    for (auto it = csd_map_.begin(); it != csd_map_.end(); ++it) {
        delete it->second;
    }
}

/**
 * CsdHandle
 */

void CsdHandle::update_stat(int st) {
    WriteLocker locker(lock_);

    stat_ = st;

    if (st == CSD_STAT_PAUSE || st == CSD_STAT_ACTIVE)
        latime_ = utime_t::now();
    
    obj_->set_stat(st);
}

void CsdHandle::update_health(const csd_hlt_sub_t& hlt) {
    WriteLocker locker(lock_);

    obj_->set_alloced(hlt.alloced);
    obj_->set_used(hlt.used);
    obj_->health().hlt_meta = hlt.hlt_meta;

    // 更新需要计算的健康信息
    obj_->add_write_count(hlt.hlt_meta.last_write);
    obj_->add_read_count(hlt.hlt_meta.last_read);
    // double u = (double)hlt.used / hlt.size;
    // obj_->set_wear_weight(obj_->get_wear_weight() + (hlt.hlt_meta.last_write / (1 - u)));

    obj_as_dirty__(); // 健康信息更新不马上写回MetaStore
}

CsdObject* CsdHandle::get() {
    return obj_readable__() ? obj_ : nullptr;
}

CsdObject* CsdHandle::read_and_lock() {
    if (!obj_readable__())
        return nullptr;
    lock_.rdlock();
    return obj_;
}

CsdObject* CsdHandle::read_try_lock() {
    return (obj_readable__() && lock_.try_rdlock()) ? obj_ : nullptr;
}

CsdObject* CsdHandle::write_and_lock() {
    if (!obj_readable__())
        return nullptr;
    lock_.wrlock();
    return obj_;
}

CsdObject* CsdHandle::write_try_lock() {
    return (obj_readable__() && lock_.try_wrlock()) ? obj_ : nullptr;
}

void CsdHandle::save_and_unlock() {
    save();
    unlock();
}

int CsdHandle::save() {
    if(!obj_readable__())
        return RC_FAILD;

    if (stat_.load() == CSD_OBJ_STAT_NEW) {
        ms_->get_csd_ms()->create(obj_->meta());
        ms_->get_csd_health_ms()->create(obj_->health());
    } else if (stat_.load() == CSD_OBJ_STAT_DIRT) {
        ms_->get_csd_ms()->update(obj_->meta());
        ms_->get_csd_health_ms()->update(obj_->health());
    } else {
        return RC_SUCCESS;
    }

    obj_as_save__();
    return RC_SUCCESS;
}

int CsdHandle::load() {
    if (stat_ != CSD_OBJ_STAT_LOAD)
        return RC_OBJ_NOT_FOUND;
    
    int r;
    if ((r = ms_->get_csd_ms()->get(obj_->meta(), csd_id_)) != 0)
        return r;
    
    if ((r = ms_->get_csd_health_ms()->get(obj_->health(), csd_id_)) != 0)
        return r;
    
    stat_ = CSD_OBJ_STAT_SVAE;

    return RC_SUCCESS;
}

int CsdHandle::connect() {
    if (!obj_readable__())
        return RC_FAILD;
    
    if (client_.get() == nullptr) 
        client_ = make_client__();
    
    return client_.get() == nullptr ? RC_FAILD : RC_SUCCESS;
}

int CsdHandle::disconnect() {
    client_ = nullptr;
    return RC_SUCCESS;
}

std::shared_ptr<CsdsClient> CsdHandle::get_client() {
    ReadLocker locker(lock_);
    if (connect() == RC_SUCCESS)
        return client_;
    return nullptr;
}

std::shared_ptr<CsdsClient> CsdHandle::make_client__() {
    node_addr_t addr = obj_->get_admin_addr();
    return csdm_->csd_client_foctory_->make_csds_client(addr);
}

} // namespace flame