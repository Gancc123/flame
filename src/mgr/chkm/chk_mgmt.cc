#include "mgr/chkm/chk_mgmt.h"
#include "include/retcode.h"
#include "util/utime.h"

#include <map>

#include "log_chkm.h"

using namespace std;

namespace flame {

int ChunkManager::create_bulk(const list<uint64_t>& chk_ids, int cgn, const chk_attr_t& attr) {
    int r;

    if (chk_ids.empty()) 
        return RC_SUCCESS;

    if (cgn <= 0 || cgn > 16) {
        bct_->log()->lerror("wrong parameter (cgn): %d", cgn);
        return RC_WRONG_PARAMETER;
    }

    int chk_num = chk_ids.size();
    if (chk_num % cgn != 0) {
        bct_->log()->lerror("wrong parameter (chd_ids.size): %u", chk_num);
        return RC_WRONG_PARAMETER;
    }

    // 将Chunk记录持久化到MetaStore并将状态标记为CREATING
    list<chunk_meta_t> chk_list;
    chunk_meta_t meta;
    meta.stat = CHK_STAT_CREATING; // 等待创建
    meta.spolicy = attr.spolicy;
    meta.flags = attr.flags;
    meta.size = attr.size;
    for (auto it = chk_ids.begin(); it != chk_ids.end(); it++) {
        chunk_id_t cid(*it);
        meta.chk_id = cid;
        meta.vol_id = cid.get_vol_id();
        meta.index = cid.get_index();
        chk_list.push_back(meta);
    }

    if ((r = ms_->get_chunk_ms()->create_bulk(chk_list)) != RC_SUCCESS) {
        bct_->log()->lerror("persist chunk bulk faild: %d", r);
        return RC_FAILD;
    }
    chk_list.clear(); // 避免占用过多内存

    // 选择合适的CSD
    bct_->log()->ltrace("select csds");
    int grp = chk_num / cgn;
    list<uint64_t> csd_list;
    if ((r = layout_->select_bulk(csd_list, grp, cgn, attr.size)) != RC_SUCCESS) {
        bct_->log()->lerror("select csd faild: %d", r);//
        return RC_FAILD;
    }

    if (csd_list.size() != chk_ids.size()) {
        bct_->log()->lerror("wrong result of chunk layout");
        return RC_FAILD;
    }

    // 匹配Chunk与CSD，并过滤为以CSD为单位
    bct_->log()->ltrace("filter csds");
    map<uint64_t, list<uint64_t>> chk_dict;
    list<uint64_t>::iterator csd_it = csd_list.begin();
    list<uint64_t>::const_iterator chk_it = chk_ids.begin();
    for (; csd_it != csd_list.end(); csd_it++, chk_it++) {
        chk_dict[*csd_it].push_back(*chk_it);
    }

    // 控制CSD创建Chunk
    bct_->log()->ltrace("control csd to create chunks");
    bool success = true;
    for (auto it = chk_dict.begin(); it != chk_dict.end(); it++) {
        CsdHandle* hdl = csdm_->find(it->first);
        if (hdl == nullptr) {
            // CSD宕机，暂时不做处理
            bct_->log()->lerror("csd shutdown: %llu", it->first);
            return RC_FAILD;
        }

        shared_ptr<CsdsClient> stub = hdl->get_client();
        if (stub.get() == nullptr) {
            // 连接断开
            bct_->log()->lerror("stub shutdown: %llu", it->first);
            return RC_FAILD;
        }

        list<chunk_bulk_res_t> res;
        r = stub->chunk_create(res, attr, it->second);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("csds chunk_create faild: %d", r);
            return RC_FAILD;
        }

        chunk_health_meta_t hlt;
        hlt.stat = CHK_STAT_CREATING;
        hlt.size = attr.size;
        for (auto rit = res.begin(); rit != res.end(); rit++) {
            if (rit->res != RC_SUCCESS) {
                bct_->log()->lerror("csds chunk_create faild with chunk: %llu : %d", rit->chk_id, rit->res);
                success = false;
            } else {
                chunk_id_t cid(rit->chk_id);
                meta.chk_id = cid;
                meta.vol_id = cid.get_vol_id();
                meta.index = cid.get_index();
                uint64_t tnow = utime_t::now().to_usec();
                meta.ctime = tnow;
                chunk_id_t pid(rit->chk_id);
                pid.set_sub_id(0);
                meta.primary = pid;
                meta.csd_id = it->first;
                meta.csd_mtime = tnow;
                meta.dst_id = it->first;
                meta.dst_ctime = tnow;
                meta.stat = CHK_STAT_CREATED;
                r = ms_->get_chunk_ms()->update(meta);
                if (r != RC_SUCCESS) {
                    bct_->log()->lerror("update chunk info faild: %llu", rit->chk_id);
                    success = false;
                    continue;
                }
                hlt.chk_id = cid;
                r = ms_->get_chunk_health_ms()->create(hlt);
                if (r != RC_SUCCESS) {
                    bct_->log()->lerror("create chunk health faild: %llu", rit->chk_id);
                    success = false;
                }
            }
        }

        if (!success) 
            break;
    }

    if (!success) {
        bct_->log()->lerror("create chunk faild");
        return RC_FAILD;
    }

    return RC_SUCCESS;
}

int ChunkManager::create_cg(chunk_id_t pid, int num, const chk_attr_t& attr) {
    list<uint64_t> chk_ids;
    for (int i = 0; i < num; i++) {
        pid.set_sub_id(i);
        chk_ids.push_back(pid);
        bct_->log()->ltrace("chk_id=%llu", pid.val);
    }

    return create_bulk(chk_ids, num, attr);
}

int ChunkManager::create_vol(chunk_id_t pid, int grp, int cgn, const chk_attr_t& attr) {
    list<uint64_t> chk_ids;
    for (int g = 0; g < grp; g++) {
         pid.set_index(pid.get_index() + cgn);
        for (int i = 0; i < cgn; i++) {
            pid.set_sub_id(i);
            chk_ids.push_back(pid);
            bct_->log()->ltrace("chk_id=%llu", pid.val);
        }
    }

    return create_bulk(chk_ids, cgn, attr);
}

int ChunkManager::info_vol(list<chunk_meta_t>& chk_list, const uint64_t& vol_id) {
    return ms_->get_chunk_ms()->list(chk_list, vol_id);
}

int ChunkManager::info_bulk(list<chunk_meta_t>& chk_list, const list<uint64_t>& chk_ids) {
    return ms_->get_chunk_ms()->list(chk_list, chk_ids);
}

int ChunkManager::update_status(const list<chk_push_attr_t>& chk_list) {
    int r;
    bool success = true;
    for (auto it = chk_list.begin(); it != chk_list.end(); it++) {
        chunk_meta_t meta;
        r = ms_->get_chunk_ms()->get(meta, it->chk_id);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("chunk metastore get faild: %llu", it->chk_id);
            success = false;
            continue;
        }

        meta.csd_id = it->csd_id;
        meta.dst_id = it->dst_id;
        meta.dst_ctime = it->dst_ctime;

        r = ms_->get_chunk_ms()->update(meta);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("chunk metastore update fiald: %llu", it->chk_id);
            success = false;
        }
    }

    return success ? RC_SUCCESS : RC_FAILD;
}

int ChunkManager::update_health(const list<chk_hlt_attr_t>& chk_hlt_list) {
    int r;
    bool success = true;
    for (auto it = chk_hlt_list.begin(); it != chk_hlt_list.end(); it++) {
        chunk_health_meta_t hlt;
        r = ms_->get_chunk_health_ms()->get(hlt, it->chk_id);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("chunk health metastore get faild: %llu", it->chk_id);
            success = false;
            continue;
        }

        hlt.grand.used = it->used;
        hlt.csd_used = it->csd_used;
        hlt.dst_used = it->dst_used;
        hlt.period = it->period;

        // 计算weight
        chk_hlt_calor_->cal_health(hlt);

        r = ms_->get_chunk_health_ms()->update(hlt);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("chunk metastore update fiald: %llu", it->chk_id);
            success = false;
        }
    }

    return success ? RC_SUCCESS : RC_FAILD;
}

int ChunkManager::update_map(const list<chk_map_t>& chk_maps) {
    int r;
    for (auto it = chk_maps.begin(); it != chk_maps.end(); it++) {
        // @@@ 缺乏事务机制
        r = ms_->get_chunk_ms()->update_map(*it);
        if (r != RC_SUCCESS) return r;
    }
    return RC_SUCCESS;
}

int ChunkManager::remove_bulk(const list<uint64_t>& chk_ids) {
    int r;
    list<chunk_meta_t> chks;
    r = ms_->get_chunk_ms()->list(chks, chk_ids);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("get chunks faild");
        return r;
    }

    return remove__(chks);
}

int ChunkManager::remove_vol(uint64_t vol_id) {
    int r;
    list<chunk_meta_t> chks;
    r = ms_->get_chunk_ms()->list(chks, vol_id);
    if (r != RC_SUCCESS) {
        bct_->log()->lerror("get chunks faild");
        return r;
    }

    return remove__(chks);
    return RC_SUCCESS;
}

int ChunkManager::remove__(list<chunk_meta_t>& chks) {
    int r;
    int faild = false; 
    map<uint64_t, list<uint64_t>> chk_dict;
    for (auto it = chks.begin(); it != chks.end(); it++) {
        it->stat = CHK_STAT_DELETING;
        chk_dict[it->csd_id].push_back(it->chk_id);
        if (it->csd_id != it->dst_id) {
            chk_dict[it->dst_id].push_back(it->chk_id);
        }
        r = ms_->get_chunk_ms()->update(*it);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("remove chunk from metastore faild: %llu", it->chk_id);
            faild = true;
        }
    }
    if (faild) return RC_INTERNAL_ERROR;

    for (auto dit = chk_dict.begin(); dit != chk_dict.end(); dit++) {
        CsdHandle* hdl = csdm_->find(dit->first);
        if (hdl == nullptr) {
            // CSD宕机，暂时不做处理
            bct_->log()->lerror("csd shutdown: %llu", dit->first);
            return RC_FAILD;
        }

        shared_ptr<CsdsClient> stub = hdl->get_client();
        if (stub.get() == nullptr) {
            // 连接断开
            bct_->log()->lerror("stub shutdown: %llu", dit->first);
            return RC_FAILD;
        }

        list<chunk_bulk_res_t> res;
        r = stub->chunk_remove(res, dit->second);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("csds chunk_remove faild: %d", r);
            return RC_FAILD;
        }

        list<uint64_t> rm_chk_ids;
        for (auto rit = res.begin(); rit != res.end(); rit++) {
            if (rit->res != RC_SUCCESS) {
                bct_->log()->lerror("csds chunk_remove faild with chunk: %llu : %d", rit->chk_id, rit->res);
                faild = true;
            } else {
                rm_chk_ids.push_back(rit->chk_id);
            }
        }

        r = ms_->get_chunk_ms()->remove_bulk(rm_chk_ids);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("remove chunks from metastore faild");
            faild = true;
            continue;
        }

        r = ms_->get_chunk_health_ms()->remove_bulk(rm_chk_ids);
        if (r != RC_SUCCESS) {
            bct_->log()->lerror("remove chunk health from metastore faild");
            faild = true;
        }
    }
    return faild ? RC_FAILD : RC_SUCCESS;
}

} //  namespace flame