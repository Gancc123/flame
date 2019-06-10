#ifndef FLAME_MEMZONE_RDMA_MEMORY_CONFIG_H
#define FLAME_MEMZONE_RDMA_MEMORY_CONFIG_H

#include "util/clog.h"
#include "util/fmt.h"
#include "common/context.h"

#include <string>
#include <vector>
#include <tuple>
#include <cassert>

#define FLAME_MEMORY_RDMA_MEM_MIN_LEVEL_D    "12"
#define FLAME_MEMORY_RDMA_MEM_MAX_LEVEL_D    "28"

namespace flame {
namespace memory {

class MemoryConfig {
private:
    FlameContext *fct;

    void perr_arg(const std::string &v) {
        clog(fmt::format("load configure file error : {}", v));
    }

public:
    static MemoryConfig *load_config(FlameContext *c) {
        auto config = new MemoryConfig(c);
        assert(config);

        if(config->load()) {
            delete config;
            return nullptr;
        }

        return config;
    }

    explicit MemoryConfig(FlameContext *c): fct(c) {

    }

    static bool get_bool(const std::string&v);

    int load();

    /**
     * RDMA RdmaBuffer min size (2^min_level)
     * @cfg: rdma_mem_min_level
     */
    uint8_t  rdma_mem_min_level;
    int set_rdma_mem_min_level(const std::string &v);

    /**
     * RDMA RdmaBuffer max size (2^max_level).
     * One BuddyAllocator will have 2^max_level bytes.
     * @cfg: rdma_mem_max_level
     */
    uint8_t  rdma_mem_max_level;
    int set_rdma_mem_max_level(const std::string &v);
};

}   //end namespace memory
}   //end namespace flame

#endif //FLAME_MEMZONE_RDMA_MEMORY_CONFIG_H