#include "mgr/csdm/csd_mgmt.h"
#include "include/retcode.h"
#include "log_csdm.h"
#include "common/context.h"

#include <cassert>
#include <algorithm>

using namespace std;

namespace flame {

/**
 * CsdManager
 */

int CsdManager::init() {
    // pass: 从MetaStore加载所有CSD信息
    // 初始化next_csd_id
    list<csd_meta_t> res_list;
    int r = ms_->get_csd_ms()->list_all(res_list);
    if (r != 0) return r;
    uint64_t max_id = 0;
    for (auto it = res_list.begin(); it != res_list.end(); ++it) {
        CsdHandle* hdl = create_csd_handle__(it->csd_id);
        hdl->set_as_down__();
        hdl->get()->meta() = *it;
        hdl->set_latime__(utime_t::get_by_usec(it->latime));
        if (!insert_csd_handle__(it->csd_id, hdl)) 
            delete hdl;
        if (max_id < it->csd_id) 
            max_id = it->csd_id;
    }

    next_csd_id_ = max_id + 1;

    list<csd_health_meta_t> hlt_list;
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

    list<uint64_t> csd_ids;
    for (int i = 1; i <= next_csd_id_; i++) {
        csd_ids.push_back(i);
    }
    list<csd_addr_t> addrs;
    csd_pull_addr(addrs, csd_ids);
    
    uint64_t new_id = next_csd_id_.fetch_add(1);

    CsdHandle* hdl = create_csd_handle__(new_id);
    hdl->set_as_none__();
    
    CsdObject* obj = hdl->get();

    obj->set_csd_id(new_id);
    obj->set_size(attr.size);
    obj->set_stat(attr.stat);
    obj->set_name(attr.csd_name);
    obj->set_io_addr(attr.io_addr);
    obj->set_admin_addr(attr.admin_addr);
    obj->set_ctime_ut(utime_t::now());
    obj->set_latime_ut(utime_t::now());
    auto meta = obj->meta();
    auto hlt = obj->health();

    bct_->log()->ldebug("meta.csd_id = %llu, hlt.csd_id = %llu", meta.csd_id, hlt.csd_id);

    ReadLocker hdl_locker(hdl->get_lock());
    int r = hdl->save_csd_info();
    if (r != RC_SUCCESS) {
        csd_map_.erase(new_id);
        bct_->log()->lerror("register csd ($llu, %s) faild: save error", new_id, attr.csd_name.c_str());
        return r;
    }

    WriteLocker map_locker(csd_map_lock_);
    if (!insert_csd_handle__(new_id, hdl)) {
        delete hdl;
        bct_->log()->lerror("register csd ($llu, %s) faild: duplicated map key", new_id, attr.csd_name.c_str());
        return RC_OBJ_EXISTED;
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
        bct_->log()->lerror("unregister csd faild: %llu", csd_id);
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
	bct_->log()->ldebug("lwg:update csd_id:%d it->second->update_stat(stat = %d)",it->first,stat);
	bct_->log()->ldebug("obj->get_name(): %s", it->second->get()->get_name().c_str());
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

int CsdManager::csd_pull_addr(list<csd_addr_t>& addrs, const list<uint64_t>& csd_ids) {
    vector<uint64_t> ids(csd_ids.begin(), csd_ids.end());
    sort(ids.begin(), ids.end());

    ReadLocker map_locker(csd_map_lock_);

    csd_addr_t addr;
    auto dit = ids.begin();
    for (auto mit = csd_map_.begin(); mit != csd_map_.end() && dit != ids.end(); mit++) {
        if (mit->second == nullptr)
            continue;
        while (*dit < mit->first) {
            addr.csd_id = *dit;
            addr.admin_addr = 0;
            addr.io_addr = 0;
            addr.stat = CSD_STAT_NONE;
            addrs.push_back(addr);
            dit++;
        }
        if (*dit == mit->first) {
            ReadLocker hdl_locker(mit->second->get_lock());
            CsdObject* obj = mit->second->get();
            addr.csd_id = mit->first;
            addr.admin_addr = obj->get_admin_addr();
            addr.io_addr = obj->get_io_addr();
            addr.stat = obj->get_stat();
            addrs.push_back(addr);
            dit++;
        }
    }

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

int CsdHandle::save_csd_info(){
    int r;
    if (stat_.load() == CSD_STAT_NONE) {
        if ((r = ms_->get_csd_ms()->create(obj_->meta())) != RC_SUCCESS)
            return r;
        if ((r = ms_->get_csd_health_ms()->create(obj_->health())) != RC_SUCCESS)
            return r;
        set_as_down__();
        return RC_SUCCESS;
    }
    return RC_OBJ_EXISTED;
}

void CsdHandle::update_stat(int st) {
    WriteLocker locker(lock_);

    stat_ = st;

    if (st == CSD_STAT_PAUSE || st == CSD_STAT_ACTIVE)
        latime_ = utime_t::now();
    obj_->set_stat(st);
	FlameContext *fct = FlameContext::get_context();
}

void CsdHandle::update_health(const csd_hlt_sub_t& hlt) {
    WriteLocker locker(lock_);

    obj_->set_alloced(hlt.alloced);
    obj_->set_used(hlt.used);
    obj_->health().period = hlt.period;

    // 更新需要计算的健康信息
    csdm_->csd_hlt_calor_->cal_health(obj_->health());

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

int CsdHandle::save_and_unlock() {
    int r = save();
    unlock();
    return r;
}

int CsdHandle::save() {
    int r;
    if(!obj_readable__())
        return RC_FAILD;

    if (obj_stat_.load() == CSD_OBJ_STAT_NEW) {
        if ((r = ms_->get_csd_ms()->create(obj_->meta())) != RC_SUCCESS)
            return r;
        if ((r = ms_->get_csd_health_ms()->create(obj_->health())) != RC_SUCCESS)
            return r;
    } else if (obj_stat_.load() == CSD_OBJ_STAT_DIRT) {
        if ((r = ms_->get_csd_ms()->update(obj_->meta())) != RC_SUCCESS)
            return r;
        if ((r = ms_->get_csd_health_ms()->update(obj_->health())) != RC_SUCCESS)
            return r;
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

shared_ptr<CsdsClient> CsdHandle::get_client() {
    ReadLocker locker(lock_);
    if (connect() == RC_SUCCESS)
        return client_;
    return nullptr;
}

shared_ptr<CsdsClient> CsdHandle::make_client__() {
    node_addr_t addr = obj_->get_admin_addr();
    return csdm_->csd_client_foctory_->make_csds_client(addr);
}

} // namespace flame
