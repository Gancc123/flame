#include "chunkstore/nvmestore/nvmeconf.h"

using namespace flame;

void NvmeConf::print_conf() {
    std::cout << "----NvmeConf info---------------\n";
    std::cout << "|file_path: "     << file_path         << "\n";
    std::cout << "|cluster_sz: "    << cluster_sz        << "\n";
    std::cout << "|num_md_pages: "  << num_md_pages      << "\n";
    std::cout << "|max_md_ops: "    << max_md_ops        << "\n";
    std::cout << "|max_channel_ops: " << max_channel_ops << "\n";
    std::cout << "|blob_id： "      << chunk_blob_map_id << "\n";
    std::cout << "|num_clusters: "  << num_clusters      << "\n";
    std::cout << "|core_mask: "     << core_mask         << "\n";
    std::cout << "|read_mask: "     << read_mask         << "\n";
    std::cout << "\t>>read_core_id_list:\n";
    for(size_t i = 0; i < read_cores.size(); i++)
        std::cout << "\t\tread_cores[" << i << "] = " << read_cores[i] << "\n";

    std::cout << "|write_mask: "    << write_mask        << "\n";
    std::cout << "\t>>write_core_id_list:\n";
    for(size_t i = 0; i < write_cores.size(); i++)
        std::cout << "\t\twrite_cores[" << i << "] = " << write_cores[i] << "\n";

    std::cout << "|meta_mask: "     << meta_mask << "\n";
    std::cout << "\t>>meta_core:\n";
    std::cout << "\t\tmeta_core = " << meta_core << "\n";
    std::cout << "--------------------------------\n";
}

uint64_t NvmeConf::get_blob_id() const {
    return chunk_blob_map_id;
}

void NvmeConf::set_blob_id(uint64_t blobid) {
    chunk_blob_map_id = blobid;
}

int NvmeConf::load() {
    std::fstream config_stream;
    config_stream.open(file_path, std::ios::in);
    if(!config_stream.is_open()) {
        return -1;
    }

    std::string dump;
    std::string key;

    while(config_stream >> key) {
        if(key == "cluster_sz") {
            config_stream >> cluster_sz;
        } else if(key == "num_md_pages") {
            config_stream >> num_md_pages;
        } else if(key == "max_md_ops") {
            config_stream >> max_md_ops;
        } else if(key == "max_channel_ops") {
            config_stream >> max_channel_ops;
        } else if(key == "chunk_map_blob_id") {
            config_stream >> chunk_blob_map_id;
        } else if(key == "chunk_map_blob_num_clusters") {
            config_stream >> num_clusters;
        } else if(key == "core_mask") {
            config_stream >> core_mask;
        } else if(key == "read_mask") {
            config_stream >> read_mask;
        } else if(key == "write_mask") {
            config_stream >> write_mask;
        } else if(key == "meta_mask") {
            config_stream >> meta_mask;
        } else if(key == "num_read_core") {
            config_stream >> num_read_core;
        } else if(key == "num_write_core") {
            config_stream >> num_write_core;
        } else {
            config_stream >> dump;
        }
    }

    config_stream.close();
    return 0;
}

void NvmeConf::set_admin_core(uint32_t core) {
    admin_core = core;
}

int NvmeConf::store() {
    std::fstream config_stream;
    config_stream.open(file_path, std::ios::out);
    if(!config_stream.is_open()) {
        return -1;
    }
    
    config_stream << "cluster_sz"        << " " << cluster_sz        << "\n";
    config_stream << "num_md_pages"      << " " << num_md_pages      << "\n";
    config_stream << "max_md_ops"        << " " << max_md_ops        << "\n";
    config_stream << "max_channel_ops"   << " " << max_channel_ops   << "\n";
    config_stream << "chunk_map_blob_id" << " " << chunk_blob_map_id << "\n";
    config_stream << "num_clusters"      << " " << num_clusters      << "\n";
    config_stream << "core_mask"         << " " << core_mask         << "\n";
    config_stream << "read_mask"         << " " << read_mask         << "\n";
    config_stream << "write_mask"        << " " << write_mask        << "\n";
    config_stream << "meta_mask "        << " " << meta_mask         << "\n";
    config_stream << "num_read_core"     << " " << num_read_core     << "\n";
    config_stream << "num_write_core"    << " " << num_write_core    << "\n";

    config_stream.close();
    return 0;
}

uint32_t NvmeConf::get_core_count() const {
    uint32_t core_mask_digit = 0;
    uint32_t core_count = 0;
    sscanf(core_mask.c_str(), "%" PRIx32, &core_mask_digit);

    while(core_mask_digit != 0) {
        if(core_mask_digit & 1)
            core_count++;
        
        core_mask_digit = core_mask_digit >> 1;
    }

    return core_count;
}

uint32_t NvmeConf::get_read_core_count() const {
    return num_read_core;
}

uint32_t NvmeConf::get_write_core_count() const {
    return num_write_core;
}

uint32_t NvmeConf::get_read_core(uint32_t index) {
    if(index >= read_cores.size())
        return read_cores[read_cores.size() - 1];
        
    return read_cores[index];
}

uint32_t NvmeConf::get_write_core(uint32_t index) {
    if(index >= write_cores.size())
        return write_cores[write_cores.size() - 1];
        
    return write_cores[index];
}

bool NvmeConf::is_valid() {
    if(access(file_path.c_str(), F_OK) != 0) {
        return false;
    }

    return true;
}

void NvmeConf::get_core_id_by_mask(std::vector<uint32_t> &cores, std::string &mask) {
    uint32_t mask_dig = 0;
    sscanf(mask.c_str(), "%" PRIx32, &mask_dig);

    uint32_t core = 0;

    while(mask_dig != 0) {
        if(((mask_dig) & (0x01)) == 0x01) {
            cores.push_back(core);
        }

        mask_dig = mask_dig >> 1;
        core++;
    }
    
    return ;
}

void NvmeConf::get_core_id_by_mask_dig(std::vector<uint32_t> &cores, uint32_t mask_dig) {
    uint32_t core = 0;
    while(mask_dig != 0) {
        if(((mask_dig) & (0x01)) == 0x01) {
            cores.push_back(core);
        }

        mask_dig = mask_dig >> 1;
        core++;
    }

    return ;
}

uint32_t NvmeConf::get_core_id_by_mask_dig(uint32_t mask_dig) {
    uint32_t core = 0;
    while(mask_dig != 0) {
        if(mask_dig & 0x01 == 0x01) {
            return core;
        }

        mask_dig = mask_dig >> 1;
        core++;
    }

    return UINT32_MAX;
}

void NvmeConf::init_core_info_default() {
    meta_core = (admin_core + 1) % core_count;
    uint32_t rest_core_count = core_count - 2;
    uint32_t core_so_far = meta_core;

    if(rest_core_count == 0) {
        //如果只有两个core，则需要meta_core和io_core共用，读写队列为同一个队列
        read_cores.push_back(core_so_far);
        write_cores.push_back(core_so_far);    
    } else if(rest_core_count == 1) {
        //如果有3个core，则meta_core和io_core分离，但是读写队列仍然为同一个队列
        core_so_far = (core_so_far + 1) % core_count;
        read_cores.push_back(core_so_far);
        write_cores.push_back(core_so_far);
    } else {
        //如果有3个以上的core，则meta_core和io_core分离，同时读写队列按照1:1的比例进行分离
        num_read_core = rest_core_count / 2;
        num_write_core = rest_core_count - num_read_core;

        for(size_t i = 0; i < num_read_core; i++) {
            core_so_far = (core_so_far + 1) % core_count;
            read_cores.push_back(core_so_far);
        }

        for(size_t i = 0; i < num_write_core; i++) {
            core_so_far = (core_so_far + 1) % core_count;
            write_cores.push_back(core_so_far);
        }
    }
}

void NvmeConf::init_core_info_core_count() {
    meta_core = (admin_core + 1) % core_count;
    //uint32_t rest_core_count = core_count - 2;
    
    uint32_t core_so_far = meta_core;

    for(size_t i = 0; i < num_read_core; i++) {
        core_so_far = (core_so_far + 1) % core_count;
        read_cores.push_back(core_so_far);
    }

    for(size_t i = 0; i < num_write_core; i++) {
        core_so_far = (core_so_far + 1) % core_count;
        write_cores.push_back(core_so_far);
    }

    return ;
}

bool NvmeConf::is_valid_mask() {
    core_count = get_core_count();
    uint32_t core_mask_dig = 0;
    sscanf(core_mask.c_str(), "%" PRIx32, &core_mask_dig);

    uint32_t read_mask_dig = 0;
    uint32_t write_mask_dig = 0;
    uint32_t meta_mask_dig = 0;
    uint32_t admin_mask_dig = 0;

    if(core_count > 2) {
        if(read_mask.empty() && write_mask.empty() && meta_mask.empty()) {
            if(num_read_core == UINT32_MAX && num_write_core == UINT32_MAX) { //**没有配置项，UINT32_MAX是默认填充的
                init_core_info_default();
                return true;
            } else if(num_read_core >= core_count || num_write_core >= core_count || num_write_core + num_read_core >= core_count) {
                return false;
            } else {
                init_core_info_core_count();
                return true;
            }
        } else {
            //如果设置了mask值，则meta_mask一定要设置，判断meta_core是否和admin_core相同
            if(!meta_mask.empty()) {
                sscanf(meta_mask.c_str(), "%" PRIx32, &meta_mask_dig);
                if(meta_mask_dig & (meta_mask_dig - 1) != 0)
                    return false;
                if(meta_mask_dig & (1UL << admin_core))
                    return false;

                //std::cout << meta_mask_dig << std::endl;;
                meta_core = get_core_id_by_mask_dig(meta_mask_dig);
                //std::cout << meta_core << std::endl;
            } else {
                return false;
            }

            //如果设置了read_mask，则判断是否和admin_core以及meta_core重合
            if(!read_mask.empty()) {
                sscanf(read_mask.c_str(), "%" PRIx32, &read_mask_dig);
                if(read_mask_dig & (1UL << admin_core) || read_mask_dig & meta_mask_dig)
                    return false;
                
                get_core_id_by_mask_dig(read_cores, read_mask_dig);
            }

            //如果设置了write_mask，则需要判断是否和admin_core，meta_core以及read_core重合
            if(!write_mask.empty()) {
                sscanf(write_mask.c_str(), "%" PRIx32, &write_mask_dig);
                if(write_mask_dig & (1UL << admin_core) || write_mask_dig & meta_mask_dig)
                    return false;

                if(read_mask_dig)
                    if(write_mask_dig & read_mask_dig)
                        return false;

                get_core_id_by_mask_dig(write_cores, write_mask_dig);
            }
        }

        return true;
    }

    return false;
}