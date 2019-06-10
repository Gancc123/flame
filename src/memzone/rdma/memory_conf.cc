#include "msg/internal/util.h"
#include "memzone/rdma/memory_conf.h"

#include <cstring>
#include <string>
#include <iostream>
#include <regex>
#include <utility>

namespace flame {
namespace memory {

bool MemoryConfig::get_bool(const std::string &v) {
    const char *true_values[] = {"yes", "enable", "on", "true", nullptr};
    auto lv = flame::msg::str2lower(v);
    const char **it = true_values;
    while(*it){
        if(lv.compare(*it) == 0){
            return true;
        }
        ++it;
    }
    return false;
}

int MemoryConfig::load(){
    int res = 0;
    auto cfg = fct->config();

    res = set_rdma_mem_min_level(cfg->get("rdma_mem_min_level", 
                                            FLAME_MEMORY_RDMA_MEM_MIN_LEVEL_D));
    if (res) {
        perr_arg("rdma_mem_min_level");
        return 1;
    }
    
    res = set_rdma_mem_max_level(cfg->get("rdma_mem_max_level", 
                                            FLAME_MEMORY_RDMA_MEM_MAX_LEVEL_D));
    if (res) {
        perr_arg("rdma_mem_max_level");
        return 1;
    }

    return 0;
}

int MemoryConfig::set_rdma_mem_min_level(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int min_level = std::stoi(v, nullptr, 0);
    if(min_level >= 0 && min_level < (1U << 8)){
        rdma_mem_min_level = min_level;
        return 0;
    }

    return 1;
}

int MemoryConfig::set_rdma_mem_max_level(const std::string &v){
    if(v.empty()){
        return 1;
    }

    int max_level = std::stoi(v, nullptr, 0);
    if(max_level >= 0 && max_level < (1U << 8)){
        rdma_mem_max_level = max_level;
        return 0;
    }

    return 1;
}

}   //namespace memory
}   //namesapce flame