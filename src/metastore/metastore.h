/*
 * @Descripttion: 
 * @version: 0.1
 * @Author: lwg
 * @Date: 2019-05-13 09:35:50
 * @LastEditors: lwg
 * @LastEditTime: 2019-08-28 14:38:46
 */
/**
 * Interface Set for MGR MetaStore
 * @version: 0.1.0
 * 
 * @Note: Return of the function/method is MSRetCode that shows the status of the call.
 * @Note: The first parameter would be the result iff the function/method have.
 */
#ifndef FLAME_METASTORE_METASTORE_H
#define FLAME_METASTORE_METASTORE_H

#include <cstdint>
#include <string>
#include <list>

#include "common/context.h"
#include "include/meta.h"

namespace flame {

/**
 * Interface for Cluster Meta Store
 */
class ClusterMS {
public:
    virtual ~ClusterMS() {}

    /**
     * Get Cluster Meta
     */
    virtual int get(cluster_meta_t& cluster) = 0;

    /**
     * Create Cluster Meta
     */
    virtual int create(const cluster_meta_t& cluster) = 0;

    /**
     * Update Cluster Meta
     */
    virtual int update(const cluster_meta_t& cluster) = 0;

protected:
    ClusterMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class ClusterMS

/**
 * Interface for Group Meta Store
 */
class VolumeGroupMS {
public:
    virtual ~VolumeGroupMS() {}

    /**
     * Get all of the volume group
     */
    virtual int list(std::list<volume_group_meta_t>& res_list, uint32_t offset = 0, uint32_t limit = 0) = 0;

    /**
     * Get a single volume group
     * @By: vg_id or name
     */
    virtual int get(volume_group_meta_t& res_vg, uint64_t vg_id) = 0;
    virtual int get(volume_group_meta_t& res_vg, const std::string& name) = 0;

    /**
     * Create a single volume group
     * @Note: create_and_get would update vg_id iff create successfully.
     */
    virtual int create(const volume_group_meta_t& new_vg) = 0;
    virtual int create_and_get(volume_group_meta_t& new_vg) = 0;

    /**
     * Remove a single volume group
     */
    virtual int remove(uint64_t vg_id) = 0;
    virtual int remove(const std::string& name) = 0;

    /**
     * Update a single volume group
     */
    virtual int update(const volume_group_meta_t& vg) = 0;

    /**
     * Rename a single volume group
     */
    virtual int rename(uint64_t vg_id, const std::string& new_name) = 0;
    virtual int rename(const std::string& old_name, const std::string& new_name) = 0;

protected:
    VolumeGroupMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class GroupMS

/**
 * Interface for Volume Meta Store
 */
class VolumeMS {
public:
    virtual ~VolumeMS() {}

    /**
     * Get all volume information in same volume group
     * @By: vg_id
     */
    virtual int list(std::list<volume_meta_t>& res_list, uint64_t vg_id, uint32_t offset = 0, uint32_t limit = 0) = 0;

    /**
     * Get a single volume
     * @By: vol_id or <vg_id, name>
     */
    virtual int get(volume_meta_t& res_vol, uint64_t vol_id) = 0;
    virtual int get(volume_meta_t& res_vol, uint64_t vg_id, const std::string& name) = 0;

    /**
     * Create a single volume
     * @Note: create_and_get would update vol_id iff create successfully.
     */
    virtual int create(const volume_meta_t& new_vol) = 0;
    virtual int create_and_get(volume_meta_t& new_vol) = 0;

    /**
     * Remove a single volume
     * @By: vol_id or <vg_id, name>
     */
    virtual int remove(uint64_t vol_id) = 0;
    virtual int remove(uint64_t vg_id, const std::string& name) = 0;

    /**
     * Update a single volume
     */
    virtual int update(const volume_meta_t& vol) = 0;

    /**
     * Rename a single volume
     */
    virtual int rename(uint64_t vol_id, const std::string& new_name) = 0;
    virtual int rename(uint64_t vg_id, const std::string& old_name, const std::string& new_name) = 0;

protected:
    VolumeMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class VolumeMS

/**
 * Interface for Chunk Meta Store
 */
class ChunkMS {
public:
    virtual ~ChunkMS() {}

    /**
     * Get all chunk information in same volume
     * @By: vol_id
     * @Note: It means that getting all iff len == 0
     */
    virtual int list(std::list<chunk_meta_t>& res_list, uint64_t vol_id, uint32_t off = 0, uint32_t len = 0) = 0;
    virtual int list(std::list<chunk_meta_t>& res_list, const std::list<uint64_t>& chk_ids) = 0;
    virtual int list_cg(std::list<chunk_meta_t>& res_list, uint64_t vol_id, uint32_t index) = 0;
    virtual int list_all(std::list<chunk_meta_t>& res_list) = 0;

    /**
     * Get a single chunk
     * @By: chk_id
     */
    virtual int get(chunk_meta_t& res_chk, uint64_t chk_id) = 0;

    /**
     * Create chunks
     * @Note: create_and_get would update chk_id iff create successfully.
     */
    virtual int create(const chunk_meta_t& new_chk) = 0;
    // create chunks with same parameter except index.
    virtual int create_bulk(const std::list<chunk_meta_t>& chk_list) = 0;

    /**
     * Remove a single chunk
     * @By: chk_id
     */
    virtual int remove(uint64_t chk_id) = 0;
    virtual int remove_vol(uint64_t vol_id) = 0;
    virtual int remove_cg(uint64_t vol_id, uint32_t index) = 0;
    virtual int remove_bulk(const std::list<uint64_t>& chk_ids) = 0;

    /**
     * Update a single chunk
     */
    virtual int update(const chunk_meta_t& chk) = 0;
    virtual int update_map(const chk_map_t& chk_map) = 0;

protected:
    ChunkMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class ChunkMS

/**
 * Interface for Chunk Health Meta Store
 */
class ChunkHealthMS {
public:
    virtual ~ChunkHealthMS() {}

    /**
     * Get a single chunk health
     * @By: chk_id
     */
    virtual int get(chunk_health_meta_t& chk_hlt, uint64_t chk_id) = 0;

    /**
     * Create a single chunk health
     * @Note: create_and_get would update chk_id iff create successfully.
     */
    virtual int create(const chunk_health_meta_t& chk_hlt) = 0;
    
    virtual int create_bulk(const std::list<chunk_health_meta_t>& chk_hlt_list) = 0;

    /**
     * Remove a single chunk health
     */
    virtual int remove(uint64_t chk_id) = 0;
    virtual int remove_bulk(const std::list<uint64_t>& chk_ids) = 0;

    /**
     * Update a single chunk health
     */
    virtual int update(const chunk_health_meta_t& chk_hlt) = 0;

    /**
     * Top
     */
    virtual int top_load(std::list<chunk_health_meta_t>& chks, uint32_t limit) = 0;
    virtual int top_wear(std::list<chunk_health_meta_t>& chks, uint32_t limit) = 0;
    virtual int top_total(std::list<chunk_health_meta_t>& chks, uint32_t limit) = 0;

protected:
    ChunkHealthMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class ChunkHealdMs

/**
 * Interface for CSD Meta Store
 */
class CsdMS {
public:
    virtual ~CsdMS() {}

    /**
     * Get CSDs for given csd_ids
     */
    virtual int list(std::list<csd_meta_t>& res_list, const std::list<uint64_t>& csd_ids) = 0;
    virtual int list_all(std::list<csd_meta_t>& res_list) = 0;

    /**
     * Get a single CSD
     */
    virtual int get(csd_meta_t& res_csd, uint64_t csd_id) = 0;

    /**
     * Create a single CSD
     */
    virtual int create(const csd_meta_t& new_csd) = 0;
    // virtual int create_and_get(csd_meta_t& new_csd) = 0;

    /**
     * Remove a single CSD
     */
    virtual int remove(uint64_t csd_id) = 0;

    /**
     * Update a single CSD
     */
    virtual int update(const csd_meta_t& csd) = 0;
    virtual int update_sm(uint64_t csd_id, uint32_t stat, uint64_t io_addr = 0, uint64_t admin_addr = 0) = 0;
    virtual int update_at(uint64_t csd_id, uint64_t latime) = 0;

protected:
    CsdMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class CsdMS

/**
 * Interface for CSD Health Meta Store
 */
class CsdHealthMS {
public:
    virtual ~CsdHealthMS() {}

    /**
     * List CSD Health
     * 
     * @Note: 'ob' == 'order by'; 'tweight' == 'total_weight'
     */
    virtual int list_all(std::list<csd_health_meta_t>& res_list) = 0;

    /**
     * Get a single CSD Health
     */
    virtual int get(csd_health_meta_t& csd_hlt, uint64_t csd_id) = 0;

    /**
     * Create a single CSD Health
     */
    virtual int create(const csd_health_meta_t& new_csd) = 0;

    /**
     * Remove a single CSD Health
     */
    virtual int remove(uint64_t csd_id) = 0;

    /**
     * Update a single CSD Health
     */
    virtual int update(const csd_health_meta_t& csd_hlt) = 0;

    /**
     * Top
     */
    virtual int top_load(std::list<csd_health_meta_t>& chks, uint32_t limit) = 0;
    virtual int top_wear(std::list<csd_health_meta_t>& chks, uint32_t limit) = 0;
    virtual int top_total(std::list<csd_health_meta_t>& chks, uint32_t limit) = 0;

protected:
    CsdHealthMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class CsdHealthMS

/**
 * Interface for Gateway Meta Store
 */
class GatewayMS {
public:
    virtual ~GatewayMS() {}

    /**
     * Get a single Gateway
     */
    virtual int get(gateway_meta_t& res_gw, uint64_t gw_id) = 0;

    /**
     * Create a single Gateway
     */
    virtual int create(const gateway_meta_t& gw) = 0;

    /**
     * Remove a single Gateway
     */
    virtual int remove(uint64_t gw_id) = 0;

    /**
     * Update a single Gateway
     */
    virtual int update(const gateway_meta_t& gw) = 0;

protected:
    GatewayMS(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class GatewayMS

class MetaStore {
public:
    virtual ~MetaStore() {}

    virtual ClusterMS* get_cluster_ms() = 0;
    virtual VolumeGroupMS* get_vg_ms() = 0;
    virtual VolumeMS* get_volume_ms() = 0;
    virtual ChunkMS* get_chunk_ms() = 0;
    virtual ChunkHealthMS* get_chunk_health_ms() = 0;
    virtual CsdMS* get_csd_ms() = 0;
    virtual CsdHealthMS* get_csd_health_ms() = 0;
    virtual GatewayMS* get_gw_ms() = 0;

    virtual int get_hot_chunk(std::map<uint64_t, uint64_t>& res, const uint64_t& csd_id, const uint16_t& limit, const uint32_t& spolicy_num) = 0;

protected:
    MetaStore(FlameContext* fct) : fct_(fct) {}

    FlameContext* fct_;
}; // class MetaStore

} // namespace flame

#endif // FLAME_METASTORE_METASTORE_H