#include "mgr/chkm/chk_mgmt.h"
#include "include/retcode.h"
#include "util/utime.h"

#include <map>

#include "log_chkm.h"

using namespace std;

namespace flame {

int ChkManager::create_bulk(const list<uint64_t>& chk_ids, int cgn, const chk_attr_t& attr) {
    int r;

    if (chk_ids.empty()) 
        return RC_SUCCESS;

    if (cgn <= 0 || cgn > 16) {
        fct_->log()->lerror("wrong parameter (cgn): %d", cgn);
        return RC_WRONG_PARAMETER;
    }

    int chk_num = chk_ids.size();
    if (chk_num % cgn != 0) {
        fct_->log()->lerror("wrong parameter (chd_ids.size): %u", chk_num);
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
        meta.csd_id = cid;
        meta.vol_id = cid.get_vol_id();
        meta.index = cid.get_index();
        chk_list.push_back(meta);
    }

    if ((r = ms_->get_chunk_ms()->create_bulk(chk_list)) != RC_SUCCESS) {
        fct_->log()->lerror("persist chunk bulk faild: %d", r);
        return RC_FAILD;
    }
    chk_list.clear(); // 避免占用过多内存

    // 选择合适的CSD
    int grp = chk_num / cgn;
    list<uint64_t> csd_list;
    if ((r = layout_->select_bulk(csd_list, grp, cgn, attr.siZe)) != RC_SUCCESS) {
        fct_->log()->lerror("select csd faild: %d", r);
        return RC_FAILD;
    }

    if (csd_list.size() != chk_ids.size()) {
        fct_->log()->lerror("wrong result of chunk layout");
        return RC_FAILD;
    }

    // 匹配Chunk与CSD，并过滤为以CSD为单位
    map<uint64_t, list<uint64_t>> chk_dict;
    for (int i = 0; i < chk_ids.size(); i++) {
        chk_dict[csd_list[i]].push_back(chk_ids[i]);
    }

    // 控制CSD创建Chunk
    bool success = true;
    for (auto it = chk_dict.begin(); it != chk_dict.end(); it++) {
        CsdHandle* hdl = csdm_.find(it->first);
        if (hdl == nullptr) {
            // CSD宕机，暂时不做处理
            fct_->log()->lerror("csd shutdown: %llu", it->first);
            return RC_FAILD;
        }

        shared_ptr<CsdsClient> stub = hdl->get_client();
        if (stub.get() == nullptr) {
            // 连接断开
            fct_->log()->lerror("stub shutdown: %llu", it->first);
            return RC_FAILD;
        }

        list<chunk_bulk_res_t> res;
        r = stub->chunk_create(res, attr, it->second);
        if (r != RC_SUCCESS) {
            fct_->log()->lerror("csds chunk_create faild: %d", r);
            return RC_FAILD;
        }

        chunk_health_meta_t hlt;
        hlt.stat = CHK_STAT_CREATING;
        hlt.size = attr.size;
        for (auto rit = res.begin(); rit != res.end(); rit++) {
            if (rit->res != RC_SUCCESS) {
                fct_->log()->lerror("csds chunk_create faild with chunk: %llu : %d", rit->chk_id, rit->res);
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
                meta.dst_mtime = tnow;
                meta.stat = CHK_STAT_CREATED;
                r = ms_->get_chunk_ms()->update(meta);
                if (r != RC_SUCCESS) {
                    fct_->log()->lerror("update chunk info faild: %llu", rit->chk_id);
                    success = false;
                    continue;
                }
                hlt.chk_id = cid;
                r = ms_->get_chunk_health_ms()->create(hlt);
                if (r != RC_SUCCESS) {
                    fct_->log()->lerror("create chunk health faild: %llu", rit->chk_id);
                    success = false;
                }
            }
        }

        if (!success) 
            break;
    }

    if (!success) {
        fct_->log()->lerror("create chunk faild");
        return RC_FAILD;
    }

    return RC_SUCCESS;
}

int ChkManager::create_cg(chunk_id_t pid, int num, const chk_attr_t& attr) {
    list<uint64_t> chk_ids;
    for (int i = 0; i < num; i++) {
        pid.set_sub_id(i);
        chk_ids.push_back(pid);
    }

    return create_bulk(chk_ids, num, attr);
}

int ChkManager::create_vol(chunk_id_t pid, int grp, int cgn, const chk_attr_t& attr) {
    list<uint64_t> chk_ids;
    for (int g = 0; g < grp; g++) {
        pid.set_index(pid.get_index() + g);
        for (int i = 0; i < cgn; i++) {
            pid.set_sub_id(i);
            chk_ids.push(pid);
        }
    }

    return create_bulk(chk_ids, cgn, attr);
}

int ChkManager::info_vol(list<chunk_meta_t>& chk_list, const uint64_t& vol_id) {
    return ms_->get_chunk_ms()->list(chk_list, vol_id);
}

int ChkManager::info_bulk(list<chunk_meta_t>& chk_list, const list<uint64_t>& chk_ids) {
    return ms_->get_chunk_ms()->list(chk_list, chk_ids);
}

int ChkManager::update_status(const list<chk_push_attr_t>& chk_list) {
    int r;
    bool success = true;
    for (auto it = chk_list.begin(); it != chk_list.end(); it++) {
        chunk_meta_t meta;
        r = ms_->get_chunk_ms()->get(meta, it->chk_id);
        if (r != RC_SUCCESS) {
            fct_->log()->lerror("chunk metastore get faild: %llu", it->chk_id);
            success = false;
            continue;
        }

        meta.csd_id = it->csd_id;
        meta.dst_id = it->dst_id;
        meta.dst_mtime = it->dst_mtime;

        r = ms_->get_chunk_ms()->update(meta);
        if (r != RC_SUCCESS) {
            fct_->log()->lerror("chunk metastore update fiald: %llu", it->chk_id);
            success = false;
        }
    }

    return success ? RC_SUCCESS : RC_FAILD;
}

int ChkManager::update_health(const list<chk_hlt_attr_t>& chk_hlt_list) {
    int r;
    bool success = true;
    for (auto it = chk_hlt_list.begin(); it != chk_hlt_list.end(); it++) {
        chunk_health_meta_t hlt;
        r = ms_->get_chunk_health_ms()->get(hlt, it->chk_id);
        if (r != RC_SUCCESS) {
            fct_->log()->lerror("chunk health metastore get faild: %llu", it->chk_id);
            success = false;
            continue;
        }

        hlt.used = it->used;
        hlt.csd_used = it->csd_used;
        hlt.dst_used = it->dst_used;
        hlt.hlt_meta = it->hlt_meta;

        // 计算weight

        r = ms_->get_chunk_health_ms()->update(hlt);
        if (r != RC_SUCCESS) {
            fct_->log()->lerror("chunk metastore update fiald: %llu", it->chk_id);
            success = false;
        }
    }

    return success ? RC_SUCCESS : RC_FAILD;
}

} //  namespace flame