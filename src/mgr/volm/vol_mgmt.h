#ifndef FLAME_VOL_MGMT_H
#define FLAME_VOL_MGMT_H

#include "common/context.h"
#include "include/meta.h"
#include "include/libflame.h"
#include "mgr/csdm/csd_mgmt.h"
#include "mgr/chkm/chk_mgmt.h"
#include "spolicy/spolicy.h"

#include <cstdint>
#include <atomic>
#include <memory>
#include <map>
#include <list>
#include <vector>

namespace flame {

class VolumeManager final {
public:
    VolumeManager(MgrBaseContext* bct, 
        const std::shared_ptr<CsdManager>& csdm,
        const std::shared_ptr<ChunkManager>& chkm)
    : bct_(bct), ms_(bct->ms()), csdm_(csdm), chkm_(chkm) {}

    /**
     * @brief 初始化
     * 
     * @return int RC_SUCCESS iff success
     */
    int init();

    /**
     * Volume Group
    */
    int vg_list(std::list<volume_group_meta_t>& res_list, uint32_t off = 0, uint32_t limit = 0);
    int vg_create(const std::string& name);
    int vg_remove(const std::string& name);
    int vg_rename(const std::string& old_name, const std::string& new_name);

    /**
     * Volume
    */
    int vol_list(std::list<volume_meta_t>& res_list, const std::string& vg_name, uint32_t offset = 0, uint32_t limit = 0);
    int vol_create(const std::string& vg_name, const std::string& vol_name, const vol_attr_t& attr);
    int vol_remove(const std::string& vg_name, const std::string& vol_name);
    int vol_rename(const std::string& vg_name, const std::string& vol_old_name, const std::string& vol_new_name);
    int vol_info(volume_meta_t& vol, const std::string& vg_name, const std::string& vol_name);
    int vol_resize(const std::string& vg_name, const std::string& vol_name, uint64_t new_size);

    int vol_open(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name, VolumeMeta &volume_meta);
    int vol_close(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name);
    int vol_lock(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name);
    int vol_unlock(uint64_t gw_id, const std::string& vg_name, const std::string& vol_name);

private:
    MgrBaseContext* bct_;
    std::shared_ptr<MetaStore> ms_;
    std::shared_ptr<CsdManager> csdm_;
    std::shared_ptr<ChunkManager> chkm_;

    /**
     * Volume Group
     */
    uint64_t max_vg_id_ {0};
    std::map<uint64_t, volume_group_meta_t> vg_map_;
    std::map<std::string, volume_group_meta_t*> vg_idx_;
    RWLock vg_lock_;

    volume_group_meta_t* get_vg__(uint64_t vg_id);
    uint64_t get_vg_id__(const std::string& vg_name);
    uint64_t get_vg_id_add_vol__(const std::string& vg_name); // 查找vg_id，同时增加vol计数
    int vg_add_vol_cnt__(uint64_t vg_id); // 增加volumes计数，不需要持久化
    int vg_add_vol_sz__(uint64_t vg_id, uint64_t sz); // 仅更新尺寸，不增加volumes，持久化
    int vg_sub_vol_cnt__(uint64_t vg_id); // 减少vol计数，但不持久化
    int vg_sub_vol__(uint64_t vg_id, uint64_t sz);

    int init_vg__();
}; // class VolumeManager

} // namespace flame

#endif // FLAME_VOL_MGMT_H